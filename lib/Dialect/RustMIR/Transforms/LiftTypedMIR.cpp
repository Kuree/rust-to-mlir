//===- LiftTypedMIR.cpp - Lift rust.mir to rust.typed ----------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/RustMIR/Transforms/Passes.h"

#include "RustToLLVM/Support/OpCreateCompat.h"
#include "mlir/Dialect/RustMIR/IR/RustMIRDialect.h"
#include "mlir/Dialect/RustMIR/IR/RustTypes.h"
#include "mlir/Dialect/RustTyped/IR/RustTypedOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"

#include <optional>
#include <iterator>
#include <string>

using namespace mlir;

namespace {
constexpr llvm::StringLiteral kRustMIRFunc("rust.mir.func");
constexpr llvm::StringLiteral kRustMIRLocal("rust.mir.local");
constexpr llvm::StringLiteral kRustMIRBlock("rust.mir.block");
constexpr llvm::StringLiteral kRustMIRAssign("rust.mir.assign");
constexpr llvm::StringLiteral kRustMIRGoto("rust.mir.goto");
constexpr llvm::StringLiteral kRustMIRSwitchInt("rust.mir.switch_int");
constexpr llvm::StringLiteral kRustMIRAssert("rust.mir.assert");
constexpr llvm::StringLiteral kRustMIRReturn("rust.mir.return");
constexpr llvm::StringLiteral
    kRustMIRUnsupportedStatement("rust.mir.unsupported_statement");
constexpr llvm::StringLiteral
    kRustMIRUnsupportedTerminator("rust.mir.unsupported_terminator");

struct LocalSlot {
  Value slot;
  Type elementType;
};

StringAttr getStringAttr(Operation *from, StringRef name) {
  return from->getAttrOfType<StringAttr>(name);
}

IntegerAttr getIntegerAttr(Operation *from, StringRef name) {
  return from->getAttrOfType<IntegerAttr>(name);
}

StringAttr getAssertMessageAttr(Operation *from, OpBuilder &builder) {
  StringAttr debugAttr = getStringAttr(from, "debug");
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

std::optional<int64_t> getInteger(Operation *op, StringRef name) {
  if (!op)
    return std::nullopt;
  auto attr = getIntegerAttr(op, name);
  if (!attr)
    return std::nullopt;
  return attr.getInt();
}

Operation *childAt(Operation *op, unsigned index) {
  if (!op || op->getNumRegions() == 0 || op->getRegion(0).empty())
    return nullptr;
  Block &block = op->getRegion(0).front();
  if (index >= block.getOperations().size())
    return nullptr;
  return &*std::next(block.begin(), index);
}

bool hasName(Operation *op, StringRef name) {
  return op && op->getName().getStringRef() == name;
}

bool hasBody(Operation *op) {
  return op && op->getNumRegions() != 0 && !op->getRegion(0).empty();
}

std::optional<std::string> getPlaceLocalName(Operation *place) {
  if (!hasName(place, "rust.mir.place"))
    return std::nullopt;
  std::optional<int64_t> local = getInteger(place, "local");
  if (!local || *local < 0)
    return std::nullopt;
  return "_" + std::to_string(*local);
}

bool hasProjection(Operation *place) {
  return hasBody(place) && !place->getRegion(0).front().empty();
}

LocalSlot *lookupSlot(llvm::StringMap<LocalSlot> &slots, StringRef name) {
  auto it = slots.find(name);
  if (it == slots.end())
    return nullptr;
  return &it->second;
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

std::optional<Type> inferPlaceType(Operation *place,
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

  for (Operation &projectionElem : place->getRegion(0).front()) {
    if (projectionElem.getName().getStringRef() != "rust.mir.projection_field")
      return std::nullopt;

    auto tupleType = dyn_cast<rust::mir::TypedTupleType>(type);
    auto indexAttr = getIntegerAttr(&projectionElem, "index");
    if (!tupleType || !indexAttr)
      return std::nullopt;

    type = tupleType.getTypeAtIndex(indexAttr);
    if (!type)
      return std::nullopt;
  }

  return type;
}

std::optional<Type> inferOperandType(Operation *operand,
                                     llvm::StringMap<LocalSlot> &slots) {
  if (hasName(operand, "rust.mir.copy") || hasName(operand, "rust.mir.move"))
    return inferPlaceType(childAt(operand, 0), slots);

  return std::nullopt;
}

std::optional<Value> materializePlaceRead(Operation *place,
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

  Block &projectionBlock = place->getRegion(0).front();
  for (auto indexedProjection : llvm::enumerate(projectionBlock)) {
    Operation &projectionElem = indexedProjection.value();
    if (projectionElem.getName().getStringRef() != "rust.mir.projection_field")
      return std::nullopt;

    std::optional<int64_t> fieldIndex = getInteger(&projectionElem, "index");
    if (!fieldIndex)
      return std::nullopt;

    Type resultType;
    if (auto tupleType = dyn_cast<rust::mir::TypedTupleType>(value.getType())) {
      auto indexAttr = getIntegerAttr(&projectionElem, "index");
      resultType = tupleType.getTypeAtIndex(indexAttr);
    }

    bool isLastProjection =
        indexedProjection.index() + 1 == projectionBlock.getOperations().size();
    if (!resultType && isLastProjection)
      resultType = expectedType;
    if (!resultType)
      return std::nullopt;

    value = mlir::rust::createOp<rust::mir::FieldOp>(
                builder, loc, resultType, value, *fieldIndex, spanAttr)
                .getResult();
  }

  return value;
}

std::optional<Value> materializeOperand(Operation *operand,
                                        OpBuilder &builder, Location loc,
                                        llvm::StringMap<LocalSlot> &slots,
                                        Type expectedType,
                                        StringAttr spanAttr) {
  if (hasName(operand, "rust.mir.copy") || hasName(operand, "rust.mir.move"))
    return materializePlaceRead(childAt(operand, 0), builder, loc, slots,
                                expectedType, spanAttr);

  if (hasName(operand, "rust.mir.constant")) {
    if (!expectedType)
      return std::nullopt;
    Attribute valueAttr = operand->getAttr("value");
    auto debugAttr = getStringAttr(operand, "debug");
    return mlir::rust::createOp<rust::mir::TypedConstOp>(
               builder, loc, expectedType, valueAttr, debugAttr, spanAttr)
        .getResult();
  }

  return std::nullopt;
}

std::optional<Value>
createTypedBinaryOp(OpBuilder &builder, Location loc, Type resultType,
                    Value lhs, Value rhs, StringRef op, StringAttr spanAttr) {
  if (op == "Add")
    return mlir::rust::createOp<rust::mir::AddOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == "AddUnchecked")
    return mlir::rust::createOp<rust::mir::AddUncheckedOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == "Sub")
    return mlir::rust::createOp<rust::mir::SubOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == "SubUnchecked")
    return mlir::rust::createOp<rust::mir::SubUncheckedOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == "Mul")
    return mlir::rust::createOp<rust::mir::MulOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == "MulUnchecked")
    return mlir::rust::createOp<rust::mir::MulUncheckedOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == "Div")
    return mlir::rust::createOp<rust::mir::DivOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == "Rem")
    return mlir::rust::createOp<rust::mir::RemOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == "BitAnd")
    return mlir::rust::createOp<rust::mir::BitAndOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == "BitOr")
    return mlir::rust::createOp<rust::mir::BitOrOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == "BitXor")
    return mlir::rust::createOp<rust::mir::BitXorOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == "Shl")
    return mlir::rust::createOp<rust::mir::ShlOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == "ShlUnchecked")
    return mlir::rust::createOp<rust::mir::ShlUncheckedOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == "Shr")
    return mlir::rust::createOp<rust::mir::ShrOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == "ShrUnchecked")
    return mlir::rust::createOp<rust::mir::ShrUncheckedOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == "Eq")
    return mlir::rust::createOp<rust::mir::EqOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == "Ne")
    return mlir::rust::createOp<rust::mir::NeOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == "Lt")
    return mlir::rust::createOp<rust::mir::LtOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == "Le")
    return mlir::rust::createOp<rust::mir::LeOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == "Gt")
    return mlir::rust::createOp<rust::mir::GtOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == "Ge")
    return mlir::rust::createOp<rust::mir::GeOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();

  return std::nullopt;
}

std::optional<Value> createTypedUnaryOp(OpBuilder &builder, Location loc,
                                        Type resultType, Value input,
                                        StringRef op, StringAttr spanAttr) {
  if (op == "Neg")
    return mlir::rust::createOp<rust::mir::NegOp>(
               builder, loc, resultType, input, spanAttr)
        .getResult();
  if (op == "Not")
    return mlir::rust::createOp<rust::mir::NotOp>(
               builder, loc, resultType, input, spanAttr)
        .getResult();
  return std::nullopt;
}

LogicalResult lowerAssign(Operation *assign, OpBuilder &builder,
                          llvm::StringMap<LocalSlot> &slots) {
  Operation *place = childAt(assign, 0);
  if (hasProjection(place))
    return assign->emitError("cannot lift assignment to projected place yet");

  std::optional<std::string> destName = getPlaceLocalName(place);
  if (!destName)
    return assign->emitError("expected assignment destination place");

  LocalSlot *dest = lookupSlot(slots, *destName);
  if (!dest)
    return assign->emitError("assignment destination has no local slot");

  Operation *rvalue = childAt(assign, 1);
  if (!rvalue)
    return assign->emitError("expected assignment rvalue operation");
  StringRef kind = rvalue->getName().getStringRef();

  if (kind == "rust.mir.binary_op" || kind == "rust.mir.checked_binary_op") {
    auto op = getStringAttr(rvalue, "op");
    Operation *lhsOp = childAt(rvalue, 0);
    Operation *rhsOp = childAt(rvalue, 1);
    if (!lhsOp || !rhsOp || !op)
      return assign->emitError("expected binary operation operands");

    std::optional<Type> lhsInferred = inferOperandType(lhsOp, slots);
    std::optional<Type> rhsInferred = inferOperandType(rhsOp, slots);
    Type resultType = dest->elementType;
    if (kind == "rust.mir.checked_binary_op")
      if (auto tupleType = dyn_cast<rust::mir::TypedTupleType>(resultType))
        resultType = tupleType.getTypeAtIndex(builder.getI64IntegerAttr(0));

    Type lhsExpected = lhsInferred.value_or(rhsInferred.value_or(resultType));
    Type rhsExpected = rhsInferred.value_or(lhsExpected);

    Location loc = assign->getLoc();
    StringAttr spanAttr = getStringAttr(assign, "span");
    std::optional<Value> lhs =
        materializeOperand(lhsOp, builder, loc, slots, lhsExpected, spanAttr);
    std::optional<Value> rhs =
        materializeOperand(rhsOp, builder, loc, slots, rhsExpected, spanAttr);
    if (!lhs || !rhs)
      return assign->emitError("failed to materialize binary operands");

    if (kind == "rust.mir.checked_binary_op") {
      Type overflowType = rust::mir::BoolType::get(assign->getContext());
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
        return assign->emitError("unsupported checked binary op: ")
               << op.getValue();
      }

      Value tuple = mlir::rust::createOp<rust::mir::MakeTupleOp>(
                        builder, loc, dest->elementType,
                        ValueRange{value, overflow}, spanAttr)
                        .getResult();
      createStore(builder, loc, tuple, *dest);
      return success();
    }

    std::optional<Value> result = createTypedBinaryOp(
        builder, loc, resultType, *lhs, *rhs, op.getValue(), spanAttr);
    if (!result)
      return assign->emitError("unsupported binary op: ") << op.getValue();
    createStore(builder, loc, *result, *dest);
    return success();
  }

  if (kind == "rust.mir.unary_op") {
    auto op = getStringAttr(rvalue, "op");
    Operation *operandOp = childAt(rvalue, 0);
    if (!operandOp || !op)
      return assign->emitError("expected unary operation operand");

    std::optional<Value> operand =
        materializeOperand(operandOp, builder, assign->getLoc(), slots,
                           dest->elementType, getStringAttr(assign, "span"));
    if (!operand)
      return assign->emitError("failed to materialize unary operand");

    std::optional<Value> result =
        createTypedUnaryOp(builder, assign->getLoc(), dest->elementType,
                           *operand, op.getValue(),
                           getStringAttr(assign, "span"));
    if (!result)
      return assign->emitError("unsupported unary op: ") << op.getValue();
    createStore(builder, assign->getLoc(), *result, *dest);
    return success();
  }

  if (kind == "rust.mir.aggregate") {
    auto aggregateKind = getStringAttr(rvalue, "aggregate_kind");
    if (!aggregateKind || aggregateKind.getValue() != "Tuple" ||
        !hasBody(rvalue))
      return assign->emitError("only tuple aggregate rvalues can be lifted");

    auto tupleType = dyn_cast<rust::mir::TypedTupleType>(dest->elementType);
    SmallVector<Value> operands;
    Block &operandBlock = rvalue->getRegion(0).front();
    operands.reserve(operandBlock.getOperations().size());
    for (auto indexedOperand : llvm::enumerate(operandBlock)) {
      Operation *operandOp = &indexedOperand.value();
      Type expectedType;
      if (tupleType)
        expectedType = tupleType.getTypeAtIndex(
            builder.getI64IntegerAttr(indexedOperand.index()));
      if (!expectedType)
        expectedType =
            inferOperandType(operandOp, slots).value_or(Type());
      if (!expectedType)
        return assign->emitError(
            "failed to infer tuple aggregate operand type");

      std::optional<Value> operand = materializeOperand(
          operandOp, builder, assign->getLoc(), slots, expectedType,
          getStringAttr(assign, "span"));
      if (!operand)
        return assign->emitError(
            "failed to materialize tuple aggregate operand");
      operands.push_back(*operand);
    }

    Value result = mlir::rust::createOp<rust::mir::MakeTupleOp>(
                       builder, assign->getLoc(), dest->elementType, operands,
                       getStringAttr(assign, "span"))
                       .getResult();
    createStore(builder, assign->getLoc(), result, *dest);
    return success();
  }

  if (kind == "rust.mir.use") {
    Operation *operandOp = childAt(rvalue, 0);
    if (!operandOp)
      return assign->emitError("expected use operand");
    std::optional<Value> value =
        materializeOperand(operandOp, builder, assign->getLoc(), slots,
                           dest->elementType, getStringAttr(assign, "span"));
    if (!value)
      return assign->emitError("failed to materialize use operand");
    createStore(builder, assign->getLoc(), *value, *dest);
    return success();
  }

  return assign->emitError("unsupported MIR rvalue op: ") << kind;
}

LogicalResult lowerReturn(Operation *ret, OpBuilder &builder,
                          llvm::StringMap<LocalSlot> &slots) {
  SmallVector<Value> values;
  if (LocalSlot *returnSlot = lookupSlot(slots, "_0")) {
    Value value = createLoad(builder, ret->getLoc(), *returnSlot);
    values.push_back(value);
  }
  mlir::rust::createOp<rust::mir::TypedReturnOp>(builder, ret->getLoc(), values,
                                                 getStringAttr(ret, "span"));
  return success();
}

LogicalResult lowerGotoLike(Operation *op, OpBuilder &builder) {
  std::optional<int64_t> target = getInteger(op, "target");
  if (!target)
    return op->emitError("expected target attribute");
  mlir::rust::createOp<rust::mir::TypedGotoOp>(
      builder, op->getLoc(), builder.getI64IntegerAttr(*target),
      getStringAttr(op, "span"));
  return success();
}

LogicalResult lowerSwitchInt(Operation *op, OpBuilder &builder,
                             llvm::StringMap<LocalSlot> &slots) {
  Operation *discrOp = childAt(op, 0);
  auto targets = op->getAttrOfType<DictionaryAttr>("targets");
  if (!discrOp || !targets)
    return op->emitError("expected switch_int discriminator and targets");

  std::optional<Type> discrType = inferOperandType(discrOp, slots);
  std::optional<Value> discr =
      materializeOperand(discrOp, builder, op->getLoc(), slots,
                         discrType.value_or(Type()), getStringAttr(op, "span"));
  if (!discr)
    return op->emitError("failed to materialize switch_int discriminator");

  mlir::rust::createOp<rust::mir::TypedSwitchIntOp>(
      builder, op->getLoc(), *discr, targets, getStringAttr(op, "span"));
  return success();
}

LogicalResult lowerAssert(Operation *op, OpBuilder &builder,
                          llvm::StringMap<LocalSlot> &slots) {
  Operation *condOp = childAt(op, 0);
  auto expected = op->getAttrOfType<BoolAttr>("expected");
  std::optional<int64_t> target = getInteger(op, "target");
  if (!condOp)
    return op->emitError("expected assert condition operand");
  if (!expected)
    return op->emitError("expected assert expected attribute");
  if (!target)
    return op->emitError("expected assert target attribute");

  Type boolType = rust::mir::BoolType::get(op->getContext());
  StringAttr spanAttr = getStringAttr(op, "span");
  std::optional<Value> cond =
      materializeOperand(condOp, builder, op->getLoc(), slots, boolType,
                         spanAttr);
  if (!cond)
    return op->emitError("failed to materialize assert condition");

  Value assertCond = *cond;
  if (!expected.getValue()) {
    assertCond = mlir::rust::createOp<rust::mir::NotOp>(
                     builder, op->getLoc(), boolType, assertCond, spanAttr)
                     .getResult();
  }

  mlir::rust::createOp<rust::mir::TypedAssertOp>(
      builder, op->getLoc(), assertCond, getAssertMessageAttr(op, builder),
      spanAttr);
  mlir::rust::createOp<rust::mir::TypedGotoOp>(
      builder, op->getLoc(), builder.getI64IntegerAttr(*target),
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
    SmallVector<Operation *> funcs;
    for (Operation &op : *module.getBody()) {
      if (op.getName().getStringRef() == kRustMIRFunc)
        funcs.push_back(&op);
    }

    OpBuilder builder(module.getContext());
    builder.setInsertionPointToEnd(module.getBody());

    for (Operation *func : funcs) {
      auto symName =
          func->getAttrOfType<StringAttr>(SymbolTable::getSymbolAttrName());
      if (!symName)
        continue;

      std::string typedName = (symName.getValue() + "_typed").str();
      auto typedFunc = mlir::rust::createOp<rust::mir::TypedFuncOp>(
          builder, func->getLoc(), typedName, getStringAttr(func, "rust_name"),
          getStringAttr(func, "signature"), getIntegerAttr(func, "arg_count"),
          getStringAttr(func, "span"));
      Region &typedBody = typedFunc.getBody();
      typedBody.push_back(new Block());
      builder.setInsertionPointToEnd(&typedBody.front());

      llvm::StringMap<LocalSlot> slots;
      Region &mirBody = func->getRegion(0);
      for (Operation &child : mirBody.front()) {
        if (child.getName().getStringRef() != kRustMIRLocal)
          continue;
        auto typeAttr = child.getAttrOfType<TypeAttr>("rust_type");
        if (!typeAttr)
          continue;

        Type elementType = typeAttr.getValue();
        auto slotType =
            rust::mir::SlotType::get(module.getContext(), elementType);

        IntegerAttr index = getIntegerAttr(&child, "index");
        if (!index)
          continue;

        auto slotOp = mlir::rust::createOp<rust::mir::LocalSlotOp>(
            builder, child.getLoc(), slotType, index,
            getStringAttr(&child, "name"), getStringAttr(&child, "mutability"),
            getStringAttr(&child, "role"), getStringAttr(&child, "span"));

        auto nameAttr = child.getAttrOfType<StringAttr>("name");
        if (nameAttr)
          slots.insert({nameAttr.getValue(), {slotOp.getSlot(), elementType}});
      }

      for (Operation &child : mirBody.front()) {
        if (child.getName().getStringRef() != kRustMIRBlock)
          continue;

        IntegerAttr index = getIntegerAttr(&child, "index");
        if (!index)
          continue;

        auto typedBlock = mlir::rust::createOp<rust::mir::TypedBlockOp>(
            builder, child.getLoc(), index, getStringAttr(&child, "span"));
        Region &typedBlockBody = typedBlock.getBody();
        typedBlockBody.push_back(new Block());
        builder.setInsertionPointToEnd(&typedBlockBody.front());

        Region &mirBlockBody = child.getRegion(0);
        bool hasTypedTerminator = false;
        for (Operation &mirOp : mirBlockBody.front()) {
          StringRef name = mirOp.getName().getStringRef();
          if (name == kRustMIRAssign) {
            if (failed(lowerAssign(&mirOp, builder, slots)))
              sawFailure = true;
          } else if (name == kRustMIRGoto) {
            if (failed(lowerGotoLike(&mirOp, builder)))
              sawFailure = true;
            hasTypedTerminator = true;
          } else if (name == kRustMIRSwitchInt) {
            if (failed(lowerSwitchInt(&mirOp, builder, slots)))
              sawFailure = true;
            hasTypedTerminator = true;
          } else if (name == kRustMIRAssert) {
            if (failed(lowerAssert(&mirOp, builder, slots)))
              sawFailure = true;
            hasTypedTerminator = true;
          } else if (name == kRustMIRReturn) {
            if (failed(lowerReturn(&mirOp, builder, slots)))
              sawFailure = true;
            hasTypedTerminator = true;
          } else if (name == kRustMIRUnsupportedStatement ||
                     name == kRustMIRUnsupportedTerminator) {
            mirOp.emitError("cannot lift unsupported MIR operation");
            sawFailure = true;
          } else if (name == "rust.mir.drop" || name == "rust.mir.call" ||
                     name == "rust.mir.inline_asm") {
            if (failed(lowerGotoLike(&mirOp, builder)))
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
