//===- LiftTypedMIR.cpp - Lift rust.mir to rust.typed ----------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/RustMIR/Transforms/Passes.h"

#include "RustToLLVM/Support/OpCreateCompat.h"
#include "mlir/Dialect/RustMIR/IR/RustMIRDialect.h"
#include "mlir/Dialect/RustMIR/IR/RustOps.h"
#include "mlir/Dialect/RustMIR/IR/RustTypes.h"
#include "mlir/Dialect/RustTyped/IR/RustTypedOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"

#include <iterator>
#include <optional>
#include <string>

using namespace mlir;

namespace {
struct LocalSlot {
  Value slot;
  Type elementType;
};

struct PlaceAddress {
  Value slot;
  Type elementType;
};

StringAttr getAssertMessageAttr(rust::mir::AssertOp assertOp,
                                OpBuilder &builder) {
  StringAttr debugAttr = assertOp.getDebugAttr();
  if (!debugAttr)
    return {};

  StringRef debug = debugAttr.getValue();
  // Match rustc's static runtime overflow panic messages from core::panicking.
  if (debug.contains("msg: Overflow(Add"))
    return builder.getStringAttr("attempt to add with overflow");
  if (debug.contains("msg: Overflow(Sub"))
    return builder.getStringAttr("attempt to subtract with overflow");
  if (debug.contains("msg: Overflow(Mul"))
    return builder.getStringAttr("attempt to multiply with overflow");

  return debugAttr;
}

Type typeFromRustDebug(MLIRContext *context, StringRef spelling) {
  StringRef s = spelling.trim();
  if (s == "()" || s.contains("RigidTy(Tuple([]))"))
    return rust::mir::UnitType::get(context);
  if (s.contains("RigidTy(Bool)"))
    return rust::mir::BoolType::get(context);
  if (s.contains("RigidTy(Int(I8))"))
    return rust::mir::IntType::get(context, "i8");
  if (s.contains("RigidTy(Int(I16))"))
    return rust::mir::IntType::get(context, "i16");
  if (s.contains("RigidTy(Int(I32))"))
    return rust::mir::IntType::get(context, "i32");
  if (s.contains("RigidTy(Int(I64))"))
    return rust::mir::IntType::get(context, "i64");
  if (s.contains("RigidTy(Int(I128))"))
    return rust::mir::IntType::get(context, "i128");
  if (s.contains("RigidTy(Int(Isize))"))
    return rust::mir::IntType::get(context, "isize");
  if (s.contains("RigidTy(Uint(U8))"))
    return rust::mir::IntType::get(context, "u8");
  if (s.contains("RigidTy(Uint(U16))"))
    return rust::mir::IntType::get(context, "u16");
  if (s.contains("RigidTy(Uint(U32))"))
    return rust::mir::IntType::get(context, "u32");
  if (s.contains("RigidTy(Uint(U64))"))
    return rust::mir::IntType::get(context, "u64");
  if (s.contains("RigidTy(Uint(U128))"))
    return rust::mir::IntType::get(context, "u128");
  if (s.contains("RigidTy(Uint(Usize))"))
    return rust::mir::IntType::get(context, "usize");
  if (s.contains("RigidTy(Ref("))
    return rust::mir::RefType::get(context, s);
  if (s.contains("RigidTy(Array("))
    return rust::mir::ArrayType::get(context, s);
  if (s.contains("RigidTy(Adt("))
    return rust::mir::AdtType::get(context, s);
  if (s.contains("RigidTy(FnDef("))
    return rust::mir::FnType::get(context, s);
  return rust::mir::OpaqueType::get(context, s);
}

std::optional<std::string> extractRustDefName(StringRef text) {
  StringRef needle("name: \"");
  size_t start = text.find(needle);
  if (start == StringRef::npos)
    return std::nullopt;
  text = text.drop_front(start + needle.size());
  size_t end = text.find('"');
  if (end == StringRef::npos)
    return std::nullopt;
  return text.take_front(end).str();
}

template <typename OpT> Operation *childAt(OpT op, unsigned index) {
  if (!op)
    return nullptr;
  Region &region = op.getBody();
  if (region.empty())
    return nullptr;
  Block &block = region.front();
  if (index >= block.getOperations().size())
    return nullptr;
  return &*std::next(block.begin(), index);
}

std::optional<std::string> extractCallRustName(rust::mir::CallOp call) {
  if (std::optional<StringRef> calleeName = call.getCalleeName())
    return calleeName->str();

  auto callee = dyn_cast_or_null<rust::mir::ConstantOp>(childAt(call, 0));
  if (!callee)
    return std::nullopt;

  if (std::optional<StringRef> ty = callee.getTy())
    if (std::optional<std::string> name = extractRustDefName(*ty))
      return name;
  if (std::optional<StringRef> debug = callee.getDebug())
    return extractRustDefName(*debug);
  return std::nullopt;
}

bool isCAbiCall(rust::mir::CallOp call) {
  if (std::optional<StringRef> abi = call.getCalleeAbi())
    return *abi == "c";
  if (std::optional<std::string> rustName = extractCallRustName(call))
    return StringRef(*rustName).contains("__rust_to_llvm_");
  return false;
}

std::string getCAbiSymbol(StringRef rustName) {
  std::pair<StringRef, StringRef> split = rustName.rsplit("::");
  if (!split.second.empty())
    return split.second.str();
  return rustName.str();
}

bool needsSymbolDisambiguator(StringRef identity) {
  return identity.contains("GenericArgs([") &&
         !identity.contains("GenericArgs([])");
}

std::string getTypedSymbol(StringRef rustName, StringRef identity) {
  std::string symbol = (rustName + "_typed").str();
  if (!needsSymbolDisambiguator(identity))
    return symbol;

  auto hash = static_cast<uint64_t>(llvm::hash_value(identity));
  symbol += "_";
  symbol += llvm::utohexstr(hash);
  return symbol;
}

std::optional<std::string> getPlaceLocalName(rust::mir::PlaceOp place) {
  if (!place)
    return std::nullopt;
  return "_" + std::to_string(static_cast<int64_t>(place.getLocal()));
}

bool hasProjection(rust::mir::PlaceOp place) {
  return place && !place.getBody().empty() && !place.getBody().front().empty();
}

void markAddressTakenPlace(Operation *op, llvm::StringSet<> &locals) {
  auto place = dyn_cast_or_null<rust::mir::PlaceOp>(op);
  if (!place)
    return;

  if (std::optional<std::string> localName = getPlaceLocalName(place))
    locals.insert(*localName);
}

llvm::StringSet<> collectAddressTakenLocals(rust::mir::FuncOp func) {
  llvm::StringSet<> locals;
  func.walk([&](rust::mir::AssignOp assign) {
    Operation *rvalue = childAt(assign, 1);
    if (auto ref = dyn_cast_or_null<rust::mir::RefOp>(rvalue)) {
      markAddressTakenPlace(childAt(ref, 0), locals);
      return;
    }
    if (auto addressOf = dyn_cast_or_null<rust::mir::AddressOfOp>(rvalue))
      markAddressTakenPlace(childAt(addressOf, 0), locals);
  });
  return locals;
}

LocalSlot *lookupSlot(llvm::StringMap<LocalSlot> &slots, StringRef name) {
  auto it = slots.find(name);
  if (it == slots.end())
    return nullptr;
  return &it->second;
}

Type getIndexedElementType(Type aggregateType, Attribute index) {
  if (auto tupleType = dyn_cast<rust::mir::TypedTupleType>(aggregateType))
    return tupleType.getTypeAtIndex(index);
  if (auto arrayType = dyn_cast<rust::mir::TypedArrayType>(aggregateType))
    return arrayType.getTypeAtIndex(index);
  return {};
}

Value createLoad(OpBuilder &builder, Location loc, LocalSlot &slot) {
  return mlir::rust::createOp<rust::mir::LoadOp>(builder, loc, slot.elementType,
                                                 slot.slot)
      .getValue();
}

void createStore(OpBuilder &builder, Location loc, Value value,
                 LocalSlot &slot) {
  mlir::rust::createOp<rust::mir::StoreOp>(builder, loc, value, slot.slot);
}

std::optional<PlaceAddress>
materializePlaceAddress(rust::mir::PlaceOp place,
                        llvm::StringMap<LocalSlot> &slots) {
  std::optional<std::string> localName = getPlaceLocalName(place);
  if (!localName)
    return std::nullopt;

  LocalSlot *slot = lookupSlot(slots, *localName);
  if (!slot)
    return std::nullopt;

  if (hasProjection(place))
    return std::nullopt;

  return PlaceAddress{slot->slot, slot->elementType};
}

std::optional<Type> inferPlaceType(rust::mir::PlaceOp place,
                                   llvm::StringMap<LocalSlot> &slots) {
  std::optional<std::string> localName = getPlaceLocalName(place);
  if (!localName)
    return std::nullopt;

  LocalSlot *slot = lookupSlot(slots, *localName);
  if (!slot)
    return std::nullopt;

  Type type = slot->elementType;
  if (!hasProjection(place))
    return type;

  for (Operation &projectionElem : place.getBody().front()) {
    auto field = dyn_cast<rust::mir::ProjectionFieldOp>(projectionElem);
    if (!field)
      return std::nullopt;

    auto indexAttr = field.getIndexAttr();
    if (!indexAttr)
      return std::nullopt;

    type = getIndexedElementType(type, indexAttr);
    if (!type)
      return std::nullopt;
  }

  return type;
}

rust::mir::PlaceOp getOperandPlace(Operation *operand) {
  if (auto copy = dyn_cast_or_null<rust::mir::CopyOp>(operand))
    return dyn_cast_or_null<rust::mir::PlaceOp>(childAt(copy, 0));
  if (auto move = dyn_cast_or_null<rust::mir::MoveOp>(operand))
    return dyn_cast_or_null<rust::mir::PlaceOp>(childAt(move, 0));
  return {};
}

std::optional<Type> inferOperandType(Operation *operand,
                                     llvm::StringMap<LocalSlot> &slots) {
  if (rust::mir::PlaceOp place = getOperandPlace(operand))
    return inferPlaceType(place, slots);

  if (auto constant = dyn_cast_or_null<rust::mir::ConstantOp>(operand))
    if (std::optional<StringRef> ty = constant.getTy())
      return typeFromRustDebug(operand->getContext(), *ty);

  return std::nullopt;
}

std::optional<Value> materializePlaceRead(rust::mir::PlaceOp place,
                                          OpBuilder &builder, Location loc,
                                          llvm::StringMap<LocalSlot> &slots,
                                          Type expectedType,
                                          StringAttr spanAttr) {
  std::optional<std::string> localName = getPlaceLocalName(place);
  if (!localName)
    return std::nullopt;

  LocalSlot *slot = lookupSlot(slots, *localName);
  if (!slot)
    return std::nullopt;

  Value value = createLoad(builder, loc, *slot);
  if (!hasProjection(place))
    return value;

  Block &projectionBlock = place.getBody().front();
  for (auto indexedProjection : llvm::enumerate(projectionBlock)) {
    Operation &projectionElem = indexedProjection.value();
    auto field = dyn_cast<rust::mir::ProjectionFieldOp>(projectionElem);
    if (!field)
      return std::nullopt;

    Type resultType =
        getIndexedElementType(value.getType(), field.getIndexAttr());

    bool isLastProjection =
        indexedProjection.index() + 1 == projectionBlock.getOperations().size();
    if (!resultType && isLastProjection)
      resultType = expectedType;
    if (!resultType)
      return std::nullopt;

    value = mlir::rust::createOp<rust::mir::FieldOp>(
                builder, loc, resultType, value,
                static_cast<int64_t>(field.getIndex()), spanAttr)
                .getResult();
  }

  return value;
}

std::optional<Value> materializeOperand(Operation *operand, OpBuilder &builder,
                                        Location loc,
                                        llvm::StringMap<LocalSlot> &slots,
                                        Type expectedType,
                                        StringAttr spanAttr) {
  if (rust::mir::PlaceOp place = getOperandPlace(operand))
    return materializePlaceRead(place, builder, loc, slots, expectedType,
                                spanAttr);

  if (auto constant = dyn_cast_or_null<rust::mir::ConstantOp>(operand)) {
    if (!expectedType) {
      if (std::optional<StringRef> ty = constant.getTy())
        expectedType = typeFromRustDebug(operand->getContext(), *ty);
    }
    if (!expectedType)
      return std::nullopt;
    Attribute valueAttr = constant.getValueAttr();
    auto debugAttr = constant.getDebugAttr();
    return mlir::rust::createOp<rust::mir::TypedConstOp>(
               builder, loc, expectedType, valueAttr, debugAttr, spanAttr)
        .getResult();
  }

  return std::nullopt;
}

std::optional<Value> createTypedBinaryOp(OpBuilder &builder, Location loc,
                                         Type resultType, Value lhs, Value rhs,
                                         StringRef op, StringAttr spanAttr) {
  if (op == "Add")
    return mlir::rust::createOp<rust::mir::AddOp>(builder, loc, resultType, lhs,
                                                  rhs, spanAttr)
        .getResult();
  if (op == "AddUnchecked")
    return mlir::rust::createOp<rust::mir::AddUncheckedOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == "Sub")
    return mlir::rust::createOp<rust::mir::SubOp>(builder, loc, resultType, lhs,
                                                  rhs, spanAttr)
        .getResult();
  if (op == "SubUnchecked")
    return mlir::rust::createOp<rust::mir::SubUncheckedOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == "Mul")
    return mlir::rust::createOp<rust::mir::MulOp>(builder, loc, resultType, lhs,
                                                  rhs, spanAttr)
        .getResult();
  if (op == "MulUnchecked")
    return mlir::rust::createOp<rust::mir::MulUncheckedOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == "Div")
    return mlir::rust::createOp<rust::mir::DivOp>(builder, loc, resultType, lhs,
                                                  rhs, spanAttr)
        .getResult();
  if (op == "Rem")
    return mlir::rust::createOp<rust::mir::RemOp>(builder, loc, resultType, lhs,
                                                  rhs, spanAttr)
        .getResult();
  if (op == "BitAnd")
    return mlir::rust::createOp<rust::mir::BitAndOp>(builder, loc, resultType,
                                                     lhs, rhs, spanAttr)
        .getResult();
  if (op == "BitOr")
    return mlir::rust::createOp<rust::mir::BitOrOp>(builder, loc, resultType,
                                                    lhs, rhs, spanAttr)
        .getResult();
  if (op == "BitXor")
    return mlir::rust::createOp<rust::mir::BitXorOp>(builder, loc, resultType,
                                                     lhs, rhs, spanAttr)
        .getResult();
  if (op == "Shl")
    return mlir::rust::createOp<rust::mir::ShlOp>(builder, loc, resultType, lhs,
                                                  rhs, spanAttr)
        .getResult();
  if (op == "ShlUnchecked")
    return mlir::rust::createOp<rust::mir::ShlUncheckedOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == "Shr")
    return mlir::rust::createOp<rust::mir::ShrOp>(builder, loc, resultType, lhs,
                                                  rhs, spanAttr)
        .getResult();
  if (op == "ShrUnchecked")
    return mlir::rust::createOp<rust::mir::ShrUncheckedOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == "Eq")
    return mlir::rust::createOp<rust::mir::EqOp>(builder, loc, resultType, lhs,
                                                 rhs, spanAttr)
        .getResult();
  if (op == "Ne")
    return mlir::rust::createOp<rust::mir::NeOp>(builder, loc, resultType, lhs,
                                                 rhs, spanAttr)
        .getResult();
  if (op == "Lt")
    return mlir::rust::createOp<rust::mir::LtOp>(builder, loc, resultType, lhs,
                                                 rhs, spanAttr)
        .getResult();
  if (op == "Le")
    return mlir::rust::createOp<rust::mir::LeOp>(builder, loc, resultType, lhs,
                                                 rhs, spanAttr)
        .getResult();
  if (op == "Gt")
    return mlir::rust::createOp<rust::mir::GtOp>(builder, loc, resultType, lhs,
                                                 rhs, spanAttr)
        .getResult();
  if (op == "Ge")
    return mlir::rust::createOp<rust::mir::GeOp>(builder, loc, resultType, lhs,
                                                 rhs, spanAttr)
        .getResult();

  return std::nullopt;
}

std::optional<Value> createTypedUnaryOp(OpBuilder &builder, Location loc,
                                        Type resultType, Value input,
                                        StringRef op, StringAttr spanAttr) {
  if (op == "Neg")
    return mlir::rust::createOp<rust::mir::NegOp>(builder, loc, resultType,
                                                  input, spanAttr)
        .getResult();
  if (op == "Not")
    return mlir::rust::createOp<rust::mir::NotOp>(builder, loc, resultType,
                                                  input, spanAttr)
        .getResult();
  return std::nullopt;
}

LogicalResult lowerAssign(rust::mir::AssignOp assign, OpBuilder &builder,
                          llvm::StringMap<LocalSlot> &slots) {
  auto place = dyn_cast_or_null<rust::mir::PlaceOp>(childAt(assign, 0));
  if (!place)
    return assign.emitError("expected assignment destination place");
  if (hasProjection(place))
    return assign.emitError("cannot lift assignment to projected place yet");

  std::optional<std::string> destName = getPlaceLocalName(place);
  if (!destName)
    return assign.emitError("expected assignment destination place");

  LocalSlot *dest = lookupSlot(slots, *destName);
  if (!dest)
    return assign.emitError("assignment destination has no local slot");

  Operation *rvalue = childAt(assign, 1);
  if (!rvalue)
    return assign.emitError("expected assignment rvalue operation");

  auto lowerBinaryRvalue = [&](auto binaryRvalue, StringAttr op,
                               bool isChecked) -> LogicalResult {
    Operation *lhsOp = childAt(binaryRvalue, 0);
    Operation *rhsOp = childAt(binaryRvalue, 1);
    if (!lhsOp || !rhsOp || !op)
      return assign.emitError("expected binary operation operands");

    std::optional<Type> lhsInferred = inferOperandType(lhsOp, slots);
    std::optional<Type> rhsInferred = inferOperandType(rhsOp, slots);
    Type resultType = dest->elementType;
    if (isChecked)
      if (auto tupleType = dyn_cast<rust::mir::TypedTupleType>(resultType))
        resultType = tupleType.getTypeAtIndex(builder.getI64IntegerAttr(0));

    Type lhsExpected = lhsInferred.value_or(rhsInferred.value_or(resultType));
    Type rhsExpected = rhsInferred.value_or(lhsExpected);

    Location loc = assign.getLoc();
    StringAttr spanAttr = assign.getSpanAttr();
    std::optional<Value> lhs =
        materializeOperand(lhsOp, builder, loc, slots, lhsExpected, spanAttr);
    std::optional<Value> rhs =
        materializeOperand(rhsOp, builder, loc, slots, rhsExpected, spanAttr);
    if (!lhs || !rhs)
      return assign.emitError("failed to materialize binary operands");

    if (isChecked) {
      Type overflowType = rust::mir::BoolType::get(assign.getContext());
      if (auto tupleType =
              dyn_cast<rust::mir::TypedTupleType>(dest->elementType)) {
        if (Type elementType =
                tupleType.getTypeAtIndex(builder.getI64IntegerAttr(0)))
          resultType = elementType;
        if (Type elementType =
                tupleType.getTypeAtIndex(builder.getI64IntegerAttr(1)))
          overflowType = elementType;
      }

      Value value;
      Value overflow;
      if (op.getValue() == "Add") {
        auto checked = mlir::rust::createOp<rust::mir::CheckedAddOp>(
            builder, loc, resultType, overflowType, *lhs, *rhs, spanAttr);
        value = checked.getValue();
        overflow = checked.getOverflow();
      } else if (op.getValue() == "Sub") {
        auto checked = mlir::rust::createOp<rust::mir::CheckedSubOp>(
            builder, loc, resultType, overflowType, *lhs, *rhs, spanAttr);
        value = checked.getValue();
        overflow = checked.getOverflow();
      } else if (op.getValue() == "Mul") {
        auto checked = mlir::rust::createOp<rust::mir::CheckedMulOp>(
            builder, loc, resultType, overflowType, *lhs, *rhs, spanAttr);
        value = checked.getValue();
        overflow = checked.getOverflow();
      } else {
        return assign.emitError("unsupported checked binary op: ")
               << op.getValue();
      }

      Value tuple = mlir::rust::createOp<rust::mir::MakeAggregateOp>(
                        builder, loc, dest->elementType,
                        ValueRange{value, overflow}, spanAttr)
                        .getResult();
      createStore(builder, loc, tuple, *dest);
      return success();
    }

    std::optional<Value> result = createTypedBinaryOp(
        builder, loc, resultType, *lhs, *rhs, op.getValue(), spanAttr);
    if (!result)
      return assign.emitError("unsupported binary op: ") << op.getValue();
    createStore(builder, loc, *result, *dest);
    return success();
  };

  if (auto binary = dyn_cast<rust::mir::BinaryOp>(rvalue))
    return lowerBinaryRvalue(binary, binary.getOpAttr(), false);
  if (auto checked = dyn_cast<rust::mir::CheckedBinaryOp>(rvalue))
    return lowerBinaryRvalue(checked, checked.getOpAttr(), true);

  if (auto unary = dyn_cast<rust::mir::UnaryOp>(rvalue)) {
    StringAttr op = unary.getOpAttr();
    Operation *operandOp = childAt(unary, 0);
    if (!operandOp || !op)
      return assign.emitError("expected unary operation operand");

    std::optional<Value> operand =
        materializeOperand(operandOp, builder, assign.getLoc(), slots,
                           dest->elementType, assign.getSpanAttr());
    if (!operand)
      return assign.emitError("failed to materialize unary operand");

    std::optional<Value> result =
        createTypedUnaryOp(builder, assign.getLoc(), dest->elementType,
                           *operand, op.getValue(), assign.getSpanAttr());
    if (!result)
      return assign.emitError("unsupported unary op: ") << op.getValue();
    createStore(builder, assign.getLoc(), *result, *dest);
    return success();
  }

  if (auto aggregate = dyn_cast<rust::mir::AggregateOp>(rvalue)) {
    StringRef aggregateKind = aggregate.getAggregateKind();
    if ((aggregateKind != "Tuple" && aggregateKind != "Array") ||
        aggregate.getBody().empty())
      return assign.emitError("only tuple and array aggregate rvalues can be "
                              "lifted");

    auto tupleType = dyn_cast<rust::mir::TypedTupleType>(dest->elementType);
    auto arrayType = dyn_cast<rust::mir::TypedArrayType>(dest->elementType);
    if (aggregateKind == "Tuple" && !tupleType)
      return assign.emitError("tuple aggregate destination is not a tuple");
    if (aggregateKind == "Array" && !arrayType)
      return assign.emitError("array aggregate destination is not an array");

    SmallVector<Value> operands;
    Block &operandBlock = aggregate.getBody().front();
    operands.reserve(operandBlock.getOperations().size());
    if (arrayType &&
        operandBlock.getOperations().size() != arrayType.getLength())
      return assign.emitError("array aggregate operand count does not match "
                              "array length");

    for (auto indexedOperand : llvm::enumerate(operandBlock)) {
      Operation *operandOp = &indexedOperand.value();
      Type expectedType;
      if (tupleType)
        expectedType = tupleType.getTypeAtIndex(
            builder.getI64IntegerAttr(indexedOperand.index()));
      if (arrayType)
        expectedType = arrayType.getElementType();
      if (!expectedType)
        expectedType = inferOperandType(operandOp, slots).value_or(Type());
      if (!expectedType)
        return assign.emitError("failed to infer aggregate operand type");

      std::optional<Value> operand =
          materializeOperand(operandOp, builder, assign.getLoc(), slots,
                             expectedType, assign.getSpanAttr());
      if (!operand)
        return assign.emitError("failed to materialize aggregate operand");
      operands.push_back(*operand);
    }

    Value result = mlir::rust::createOp<rust::mir::MakeAggregateOp>(
                       builder, assign.getLoc(), dest->elementType, operands,
                       assign.getSpanAttr())
                       .getResult();
    createStore(builder, assign.getLoc(), result, *dest);
    return success();
  }

  if (auto ref = dyn_cast<rust::mir::RefOp>(rvalue)) {
    auto sourcePlace = dyn_cast_or_null<rust::mir::PlaceOp>(childAt(ref, 0));
    if (!sourcePlace)
      return assign.emitError("expected ref source place");

    std::optional<PlaceAddress> address =
        materializePlaceAddress(sourcePlace, slots);
    if (!address)
      return assign.emitError("failed to materialize ref source address");

    auto refType = dyn_cast<rust::mir::TypedRefType>(dest->elementType);
    if (!refType)
      return assign.emitError("ref destination is not a typed reference");
    if (address->elementType != refType.getPointeeType())
      return assign.emitError("ref source type does not match destination");

    Value result =
        mlir::rust::createOp<rust::mir::BorrowOp>(
            builder, assign.getLoc(), dest->elementType, address->slot,
            ref.getBorrowKindAttr(), ref.getMutabilityAttr(),
            ref.getRustRegionAttr(), assign.getSpanAttr())
            .getResult();
    createStore(builder, assign.getLoc(), result, *dest);
    return success();
  }

  if (auto addressOf = dyn_cast<rust::mir::AddressOfOp>(rvalue)) {
    auto sourcePlace =
        dyn_cast_or_null<rust::mir::PlaceOp>(childAt(addressOf, 0));
    if (!sourcePlace)
      return assign.emitError("expected address_of source place");

    std::optional<PlaceAddress> address =
        materializePlaceAddress(sourcePlace, slots);
    if (!address)
      return assign.emitError("failed to materialize address_of source");

    auto rawPtrType = dyn_cast<rust::mir::TypedRawPtrType>(dest->elementType);
    if (!rawPtrType)
      return assign.emitError("address_of destination is not a typed raw "
                              "pointer");
    if (address->elementType != rawPtrType.getPointeeType())
      return assign.emitError("address_of source type does not match "
                              "destination");

    Value result = mlir::rust::createOp<rust::mir::RawAddressOp>(
                       builder, assign.getLoc(), dest->elementType,
                       address->slot, addressOf.getRawPtrKindAttr(),
                       addressOf.getMutabilityAttr(), assign.getSpanAttr())
                       .getResult();
    createStore(builder, assign.getLoc(), result, *dest);
    return success();
  }

  if (auto use = dyn_cast<rust::mir::UseOp>(rvalue)) {
    Operation *operandOp = childAt(use, 0);
    if (!operandOp)
      return assign.emitError("expected use operand");
    std::optional<Value> value =
        materializeOperand(operandOp, builder, assign.getLoc(), slots,
                           dest->elementType, assign.getSpanAttr());
    if (!value)
      return assign.emitError("failed to materialize use operand");
    createStore(builder, assign.getLoc(), *value, *dest);
    return success();
  }

  return assign.emitError("unsupported MIR rvalue op: ") << rvalue->getName();
}

LogicalResult lowerReturn(rust::mir::ReturnOp ret, OpBuilder &builder,
                          llvm::StringMap<LocalSlot> &slots) {
  SmallVector<Value> values;
  if (LocalSlot *returnSlot = lookupSlot(slots, "_0")) {
    if (isa<rust::mir::UnitType, rust::mir::NeverType>(
            returnSlot->elementType)) {
      mlir::rust::createOp<rust::mir::TypedReturnOp>(builder, ret.getLoc(),
                                                     values, ret.getSpanAttr());
      return success();
    }
    Value value = createLoad(builder, ret.getLoc(), *returnSlot);
    values.push_back(value);
  }
  mlir::rust::createOp<rust::mir::TypedReturnOp>(builder, ret.getLoc(), values,
                                                 ret.getSpanAttr());
  return success();
}

template <typename TargetOpT>
LogicalResult lowerGotoLike(TargetOpT op, OpBuilder &builder) {
  mlir::rust::createOp<rust::mir::TypedGotoOp>(
      builder, op.getLoc(),
      builder.getI64IntegerAttr(static_cast<int64_t>(op.getTarget())),
      op.getSpanAttr());
  return success();
}

LogicalResult lowerCall(rust::mir::CallOp op, OpBuilder &builder,
                        llvm::StringMap<LocalSlot> &slots) {
  std::optional<std::string> rustName = extractCallRustName(op);
  std::optional<uint64_t> target = op.getTarget();
  auto destination = dyn_cast_or_null<rust::mir::PlaceOp>(childAt(op, 1));
  if (!rustName)
    return op.emitError("expected direct FnDef call callee");
  if (!target)
    return op.emitError("cannot lift call without return target yet");
  if (!destination)
    return op.emitError("expected call destination place");

  Location loc = op.getLoc();
  StringAttr spanAttr = op.getSpanAttr();
  SmallVector<Value> args;
  Block &callBody = op.getBody().front();
  for (auto indexedChild : llvm::enumerate(callBody)) {
    if (indexedChild.index() < 2)
      continue;
    Operation *argOp = &indexedChild.value();
    Type expectedType = inferOperandType(argOp, slots).value_or(Type());
    std::optional<Value> arg =
        materializeOperand(argOp, builder, loc, slots, expectedType, spanAttr);
    if (!arg)
      return op.emitError("failed to materialize call argument");
    args.push_back(*arg);
  }

  SmallVector<Type> resultTypes;
  std::optional<Type> destinationType = inferPlaceType(destination, slots);
  if (!destinationType)
    return op.emitError("failed to infer call destination type");
  if (!isa<rust::mir::UnitType, rust::mir::NeverType>(*destinationType))
    resultTypes.push_back(*destinationType);

  StringRef rustNameRef(*rustName);
  bool isCAbi = isCAbiCall(op);
  std::optional<StringRef> calleeType = op.getCalleeType();
  std::string callee =
      isCAbi ? getCAbiSymbol(rustNameRef)
             : getTypedSymbol(rustNameRef, calleeType.value_or(StringRef()));
  auto typedCall = mlir::rust::createOp<rust::mir::TypedCallOp>(
      builder, loc, resultTypes,
      FlatSymbolRefAttr::get(op.getContext(), callee), args,
      builder.getStringAttr(*rustName),
      builder.getStringAttr(isCAbi ? "c" : "rust"), op.getCalleeDefAttr(),
      op.getCalleeTypeAttr(), op.getCalleeGenericArgsAttr(),
      op.getCalleeInputsAttr(), op.getCalleeOutputAttr(),
      op.getCalleeCVariadicAttr(), op.getTargetAttr(), op.getUnwindAttr(),
      spanAttr);

  if (!resultTypes.empty()) {
    if (hasProjection(destination))
      return op.emitError("cannot lift call destination projection yet");
    std::optional<std::string> destName = getPlaceLocalName(destination);
    if (!destName)
      return op.emitError("expected call destination local");
    LocalSlot *dest = lookupSlot(slots, *destName);
    if (!dest)
      return op.emitError("call destination has no local slot");
    createStore(builder, loc, typedCall->getResult(0), *dest);
  }

  mlir::rust::createOp<rust::mir::TypedGotoOp>(
      builder, loc, builder.getI64IntegerAttr(static_cast<int64_t>(*target)),
      spanAttr);
  return success();
}

LogicalResult lowerSwitchInt(rust::mir::SwitchIntOp op, OpBuilder &builder,
                             llvm::StringMap<LocalSlot> &slots) {
  Operation *discrOp = childAt(op, 0);
  auto targets = op.getTargetsAttr();
  if (!discrOp || !targets)
    return op.emitError("expected switch_int discriminator and targets");

  std::optional<Type> discrType = inferOperandType(discrOp, slots);
  std::optional<Value> discr =
      materializeOperand(discrOp, builder, op.getLoc(), slots,
                         discrType.value_or(Type()), op.getSpanAttr());
  if (!discr)
    return op.emitError("failed to materialize switch_int discriminator");

  mlir::rust::createOp<rust::mir::TypedSwitchIntOp>(
      builder, op.getLoc(), *discr, targets, op.getSpanAttr());
  return success();
}

LogicalResult lowerAssert(rust::mir::AssertOp op, OpBuilder &builder,
                          llvm::StringMap<LocalSlot> &slots) {
  Operation *condOp = childAt(op, 0);
  if (!condOp)
    return op.emitError("expected assert condition operand");

  Type boolType = rust::mir::BoolType::get(op.getContext());
  StringAttr spanAttr = op.getSpanAttr();
  std::optional<Value> cond = materializeOperand(condOp, builder, op.getLoc(),
                                                 slots, boolType, spanAttr);
  if (!cond)
    return op.emitError("failed to materialize assert condition");

  Value assertCond = *cond;
  if (!op.getExpected()) {
    assertCond = mlir::rust::createOp<rust::mir::NotOp>(
                     builder, op.getLoc(), boolType, assertCond, spanAttr)
                     .getResult();
  }

  mlir::rust::createOp<rust::mir::TypedAssertOp>(
      builder, op.getLoc(), assertCond, getAssertMessageAttr(op, builder),
      spanAttr);
  mlir::rust::createOp<rust::mir::TypedGotoOp>(
      builder, op.getLoc(),
      builder.getI64IntegerAttr(static_cast<int64_t>(op.getTarget())),
      spanAttr);
  return success();
}

struct LiftTypedMIRPass
    : public PassWrapper<LiftTypedMIRPass, OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(LiftTypedMIRPass)

  StringRef getArgument() const final { return "rust-lift-typed-mir"; }
  StringRef getDescription() const final {
    return "Lift supported rust.mir mirror ops into typed rust.typed ops";
  }

  void getDependentDialects(DialectRegistry &registry) const final {
    registry.insert<rust::mir::RustMIRDialect>();
  }

  void runOnOperation() final {
    ModuleOp module = getOperation();
    bool sawFailure = false;
    SmallVector<rust::mir::FuncOp> funcs;
    for (Operation &op : *module.getBody())
      if (auto func = dyn_cast<rust::mir::FuncOp>(op))
        funcs.push_back(func);

    OpBuilder builder(module.getContext());
    builder.setInsertionPointToEnd(module.getBody());

    for (rust::mir::FuncOp func : funcs) {
      StringAttr symName = func.getSymNameAttr();
      if (!symName)
        continue;

      StringRef signature = func.getSignature().value_or(StringRef());
      std::string typedName = getTypedSymbol(symName.getValue(), signature);
      auto typedFunc = mlir::rust::createOp<rust::mir::TypedFuncOp>(
          builder, func.getLoc(), typedName, func.getRustNameAttr(),
          func.getSignatureAttr(), func.getArgCountAttr(), func.getSpanAttr());
      Region &typedBody = typedFunc.getBody();
      typedBody.push_back(new Block());
      builder.setInsertionPointToEnd(&typedBody.front());

      llvm::StringSet<> addressTakenLocals = collectAddressTakenLocals(func);
      llvm::StringMap<LocalSlot> slots;
      Region &mirBody = func.getBody();
      for (Operation &child : mirBody.front()) {
        auto local = dyn_cast<rust::mir::LocalOp>(child);
        if (!local)
          continue;
        auto typeAttr = dyn_cast_or_null<TypeAttr>(local.getRustTypeAttr());
        if (!typeAttr)
          continue;

        Type elementType = typeAttr.getValue();
        auto slotType =
            rust::mir::SlotType::get(module.getContext(), elementType);

        IntegerAttr index = local.getIndexAttr();
        if (!index)
          continue;

        BoolAttr addressTakenAttr;
        if (auto nameAttr = local.getNameAttr())
          if (addressTakenLocals.contains(nameAttr.getValue()))
            addressTakenAttr = builder.getBoolAttr(true);

        auto slotOp = mlir::rust::createOp<rust::mir::LocalSlotOp>(
            builder, child.getLoc(), slotType, index, local.getNameAttr(),
            local.getMutabilityAttr(), local.getRoleAttr(), local.getSpanAttr(),
            addressTakenAttr);

        auto nameAttr = local.getNameAttr();
        if (nameAttr)
          slots.insert({nameAttr.getValue(), {slotOp.getSlot(), elementType}});
      }

      for (Operation &child : mirBody.front()) {
        auto blockOp = dyn_cast<rust::mir::BlockOp>(child);
        if (!blockOp)
          continue;

        IntegerAttr index = blockOp.getIndexAttr();
        if (!index)
          continue;

        auto typedBlock = mlir::rust::createOp<rust::mir::TypedBlockOp>(
            builder, child.getLoc(), index, blockOp.getSpanAttr());
        Region &typedBlockBody = typedBlock.getBody();
        typedBlockBody.push_back(new Block());
        builder.setInsertionPointToEnd(&typedBlockBody.front());

        Region &mirBlockBody = blockOp.getBody();
        bool hasTypedTerminator = false;
        for (Operation &mirOp : mirBlockBody.front()) {
          if (auto assign = dyn_cast<rust::mir::AssignOp>(mirOp)) {
            if (failed(lowerAssign(assign, builder, slots)))
              sawFailure = true;
          } else if (auto gotoOp = dyn_cast<rust::mir::GotoOp>(mirOp)) {
            if (failed(lowerGotoLike(gotoOp, builder)))
              sawFailure = true;
            hasTypedTerminator = true;
          } else if (auto switchOp = dyn_cast<rust::mir::SwitchIntOp>(mirOp)) {
            if (failed(lowerSwitchInt(switchOp, builder, slots)))
              sawFailure = true;
            hasTypedTerminator = true;
          } else if (auto assertOp = dyn_cast<rust::mir::AssertOp>(mirOp)) {
            if (failed(lowerAssert(assertOp, builder, slots)))
              sawFailure = true;
            hasTypedTerminator = true;
          } else if (auto returnOp = dyn_cast<rust::mir::ReturnOp>(mirOp)) {
            if (failed(lowerReturn(returnOp, builder, slots)))
              sawFailure = true;
            hasTypedTerminator = true;
          } else if (auto call = dyn_cast<rust::mir::CallOp>(mirOp)) {
            if (failed(lowerCall(call, builder, slots)))
              sawFailure = true;
            hasTypedTerminator = true;
          } else if (isa<rust::mir::UnsupportedStatementOp,
                         rust::mir::UnsupportedTerminatorOp>(mirOp)) {
            mirOp.emitError("cannot lift unsupported MIR operation");
            sawFailure = true;
          } else if (auto drop = dyn_cast<rust::mir::DropOp>(mirOp)) {
            if (failed(lowerGotoLike(drop, builder)))
              sawFailure = true;
            hasTypedTerminator = true;
          } else if (auto inlineAsm = dyn_cast<rust::mir::InlineAsmOp>(mirOp)) {
            if (failed(lowerGotoLike(inlineAsm, builder)))
              sawFailure = true;
            hasTypedTerminator = true;
          } else if (mirOp.hasTrait<OpTrait::IsTerminator>()) {
            hasTypedTerminator = true;
          }
        }
        if (!hasTypedTerminator) {
          child.emitError("expected lifted rust.mir.block to contain a "
                          "supported terminator");
          sawFailure = true;
        }

        builder.setInsertionPointToEnd(&typedBody.front());
      }

      builder.setInsertionPointToEnd(module.getBody());
    }

    if (sawFailure)
      signalPassFailure();
  }
};
} // namespace

std::unique_ptr<Pass> mlir::rust::createLiftTypedMIRPass() {
  return std::make_unique<LiftTypedMIRPass>();
}

void mlir::rust::registerRustMIRPasses() {
  PassRegistration<LiftTypedMIRPass>();
}
