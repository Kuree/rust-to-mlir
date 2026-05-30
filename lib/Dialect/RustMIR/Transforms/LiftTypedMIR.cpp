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

std::optional<std::string> getString(DictionaryAttr dict, StringRef name) {
  if (!dict)
    return std::nullopt;
  auto attr = dyn_cast_or_null<StringAttr>(dict.get(name));
  if (!attr)
    return std::nullopt;
  return attr.getValue().str();
}

std::optional<int64_t> getInteger(DictionaryAttr dict, StringRef name) {
  if (!dict)
    return std::nullopt;
  auto attr = dyn_cast_or_null<IntegerAttr>(dict.get(name));
  if (!attr)
    return std::nullopt;
  return attr.getInt();
}

DictionaryAttr getDictionary(DictionaryAttr dict, StringRef name) {
  if (!dict)
    return {};
  return dyn_cast_or_null<DictionaryAttr>(dict.get(name));
}

ArrayAttr getArray(DictionaryAttr dict, StringRef name) {
  if (!dict)
    return {};
  return dyn_cast_or_null<ArrayAttr>(dict.get(name));
}

std::optional<std::string> getPlaceLocalName(DictionaryAttr place) {
  std::optional<int64_t> local = getInteger(place, "local");
  if (!local || *local < 0)
    return std::nullopt;
  return "_" + std::to_string(*local);
}

bool hasProjection(DictionaryAttr place) {
  ArrayAttr projection = getArray(place, "projection");
  return projection && !projection.empty();
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

std::optional<Type> inferPlaceType(DictionaryAttr place,
                                   llvm::StringMap<LocalSlot> &slots) {
  std::optional<std::string> localName = getPlaceLocalName(place);
  if (!localName)
    return std::nullopt;

  LocalSlot *slot = lookupSlot(slots, *localName);
  if (!slot)
    return std::nullopt;

  Type type = slot->elementType;
  ArrayAttr projection = getArray(place, "projection");
  if (!projection)
    return type;

  for (Attribute projectionAttr : projection) {
    auto projectionElem = dyn_cast<DictionaryAttr>(projectionAttr);
    std::optional<std::string> kind = getString(projectionElem, "kind");
    if (!kind || *kind != "Field")
      return std::nullopt;

    auto tupleType = dyn_cast<rust::mir::TypedTupleType>(type);
    auto indexAttr = dyn_cast_or_null<IntegerAttr>(projectionElem.get("index"));
    if (!tupleType || !indexAttr)
      return std::nullopt;

    type = tupleType.getTypeAtIndex(indexAttr);
    if (!type)
      return std::nullopt;
  }

  return type;
}

std::optional<Type> inferOperandType(Attribute operandAttr,
                                     llvm::StringMap<LocalSlot> &slots) {
  auto operand = dyn_cast_or_null<DictionaryAttr>(operandAttr);
  std::optional<std::string> kind = getString(operand, "kind");
  if (!kind)
    return std::nullopt;

  if (*kind == "Copy" || *kind == "Move")
    return inferPlaceType(getDictionary(operand, "place"), slots);

  return std::nullopt;
}

std::optional<Value> materializePlaceRead(DictionaryAttr place,
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
  ArrayAttr projection = getArray(place, "projection");
  if (!projection)
    return value;

  for (auto indexedProjection : llvm::enumerate(projection)) {
    auto projectionElem = dyn_cast<DictionaryAttr>(indexedProjection.value());
    std::optional<std::string> kind = getString(projectionElem, "kind");
    if (!kind || *kind != "Field")
      return std::nullopt;

    std::optional<int64_t> fieldIndex = getInteger(projectionElem, "index");
    if (!fieldIndex)
      return std::nullopt;

    Type resultType;
    if (auto tupleType = dyn_cast<rust::mir::TypedTupleType>(value.getType())) {
      auto indexAttr =
          dyn_cast_or_null<IntegerAttr>(projectionElem.get("index"));
      resultType = tupleType.getTypeAtIndex(indexAttr);
    }

    bool isLastProjection = indexedProjection.index() + 1 == projection.size();
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

std::optional<Value> materializeOperand(Attribute operandAttr,
                                        OpBuilder &builder, Location loc,
                                        llvm::StringMap<LocalSlot> &slots,
                                        Type expectedType,
                                        StringAttr spanAttr) {
  auto operand = dyn_cast_or_null<DictionaryAttr>(operandAttr);
  std::optional<std::string> kind = getString(operand, "kind");
  if (!kind)
    return std::nullopt;

  if (*kind == "Copy" || *kind == "Move")
    return materializePlaceRead(getDictionary(operand, "place"), builder, loc,
                                slots, expectedType, spanAttr);

  if (*kind == "Constant") {
    if (!expectedType)
      return std::nullopt;
    Attribute valueAttr = operand.get("value");
    auto debugAttr = dyn_cast_or_null<StringAttr>(operand.get("debug"));
    return mlir::rust::createOp<rust::mir::TypedConstOp>(
               builder, loc, expectedType, valueAttr, debugAttr, spanAttr)
        .getResult();
  }

  return std::nullopt;
}

LogicalResult lowerAssign(Operation *assign, OpBuilder &builder,
                          llvm::StringMap<LocalSlot> &slots) {
  DictionaryAttr payload = assign->getAttrOfType<DictionaryAttr>("payload");
  if (!payload)
    return assign->emitError("expected rust.mir.assign payload");

  DictionaryAttr place = getDictionary(payload, "place");
  if (hasProjection(place))
    return assign->emitError("cannot lift assignment to projected place yet");

  std::optional<std::string> destName = getPlaceLocalName(place);
  if (!destName)
    return assign->emitError("expected assignment destination place");

  LocalSlot *dest = lookupSlot(slots, *destName);
  if (!dest)
    return assign->emitError("assignment destination has no local slot");

  auto rvalue = dyn_cast_or_null<DictionaryAttr>(payload.get("rvalue"));
  std::optional<std::string> kind = getString(rvalue, "kind");
  if (!kind)
    return assign->emitError("expected assignment rvalue kind");

  if (*kind == "BinaryOp" || *kind == "CheckedBinaryOp") {
    std::optional<std::string> op = getString(rvalue, "op");
    Attribute lhsAttr = rvalue.get("lhs");
    Attribute rhsAttr = rvalue.get("rhs");
    if (!lhsAttr || !rhsAttr || !op)
      return assign->emitError("expected binary operation operands");

    std::optional<Type> lhsInferred = inferOperandType(lhsAttr, slots);
    std::optional<Type> rhsInferred = inferOperandType(rhsAttr, slots);
    Type resultType = dest->elementType;
    if (*kind == "CheckedBinaryOp")
      if (auto tupleType = dyn_cast<rust::mir::TypedTupleType>(resultType))
        resultType = tupleType.getTypeAtIndex(builder.getI64IntegerAttr(0));

    Type lhsExpected = lhsInferred.value_or(rhsInferred.value_or(resultType));
    Type rhsExpected = rhsInferred.value_or(lhsExpected);

    Location loc = assign->getLoc();
    StringAttr spanAttr = getStringAttr(assign, "span");
    std::optional<Value> lhs =
        materializeOperand(lhsAttr, builder, loc, slots, lhsExpected, spanAttr);
    std::optional<Value> rhs =
        materializeOperand(rhsAttr, builder, loc, slots, rhsExpected, spanAttr);
    if (!lhs || !rhs)
      return assign->emitError("failed to materialize binary operands");

    Value result = mlir::rust::createOp<rust::mir::BinOp>(
                       builder, loc, resultType, *lhs, *rhs, *op, spanAttr)
                       .getResult();
    if (*kind == "CheckedBinaryOp") {
      Type overflowType = rust::mir::BoolType::get(assign->getContext());
      if (auto tupleType =
              dyn_cast<rust::mir::TypedTupleType>(dest->elementType))
        if (Type elementType =
                tupleType.getTypeAtIndex(builder.getI64IntegerAttr(1)))
          overflowType = elementType;
      Value overflow = mlir::rust::createOp<rust::mir::TypedConstOp>(
                           builder, loc, overflowType, Attribute(),
                           builder.getStringAttr("false"), spanAttr)
                           .getResult();
      result = mlir::rust::createOp<rust::mir::MakeTupleOp>(
                   builder, loc, dest->elementType,
                   ValueRange{result, overflow}, spanAttr)
                   .getResult();
    }
    createStore(builder, loc, result, *dest);
    return success();
  }

  if (*kind == "UnaryOp") {
    std::optional<std::string> op = getString(rvalue, "op");
    Attribute operandAttr = rvalue.get("operand");
    if (!operandAttr || !op)
      return assign->emitError("expected unary operation operand");

    std::optional<Value> operand =
        materializeOperand(operandAttr, builder, assign->getLoc(), slots,
                           dest->elementType, getStringAttr(assign, "span"));
    if (!operand)
      return assign->emitError("failed to materialize unary operand");

    Value result = mlir::rust::createOp<rust::mir::UnOp>(
                       builder, assign->getLoc(), dest->elementType, *operand,
                       *op, getStringAttr(assign, "span"))
                       .getResult();
    createStore(builder, assign->getLoc(), result, *dest);
    return success();
  }

  if (*kind == "Aggregate") {
    std::optional<std::string> aggregateKind =
        getString(rvalue, "aggregate_kind");
    ArrayAttr operandsAttr = getArray(rvalue, "operands");
    if (!aggregateKind || *aggregateKind != "Tuple" || !operandsAttr)
      return assign->emitError("only tuple aggregate rvalues can be lifted");

    auto tupleType = dyn_cast<rust::mir::TypedTupleType>(dest->elementType);
    SmallVector<Value> operands;
    operands.reserve(operandsAttr.size());
    for (auto indexedOperand : llvm::enumerate(operandsAttr)) {
      Type expectedType;
      if (tupleType)
        expectedType = tupleType.getTypeAtIndex(
            builder.getI64IntegerAttr(indexedOperand.index()));
      if (!expectedType)
        expectedType =
            inferOperandType(indexedOperand.value(), slots).value_or(Type());
      if (!expectedType)
        return assign->emitError(
            "failed to infer tuple aggregate operand type");

      std::optional<Value> operand = materializeOperand(
          indexedOperand.value(), builder, assign->getLoc(), slots,
          expectedType, getStringAttr(assign, "span"));
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

  if (*kind == "Use") {
    Attribute operandAttr = rvalue.get("operand");
    if (!operandAttr)
      return assign->emitError("expected use operand");
    std::optional<Value> value =
        materializeOperand(operandAttr, builder, assign->getLoc(), slots,
                           dest->elementType, getStringAttr(assign, "span"));
    if (!value)
      return assign->emitError("failed to materialize use operand");
    createStore(builder, assign->getLoc(), *value, *dest);
    return success();
  }

  return assign->emitError("unsupported MIR rvalue kind: ") << *kind;
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
  DictionaryAttr payload = op->getAttrOfType<DictionaryAttr>("payload");
  std::optional<int64_t> target = getInteger(payload, "target");
  if (!target)
    return op->emitError("expected target payload");
  mlir::rust::createOp<rust::mir::TypedGotoOp>(
      builder, op->getLoc(), builder.getI64IntegerAttr(*target),
      getStringAttr(op, "span"));
  return success();
}

LogicalResult lowerSwitchInt(Operation *op, OpBuilder &builder,
                             llvm::StringMap<LocalSlot> &slots) {
  DictionaryAttr payload = op->getAttrOfType<DictionaryAttr>("payload");
  Attribute discrAttr = payload ? payload.get("discr") : Attribute();
  auto targets = getDictionary(payload, "targets");
  if (!discrAttr || !targets)
    return op->emitError("expected switch_int discriminator and targets");

  std::optional<Type> discrType = inferOperandType(discrAttr, slots);
  std::optional<Value> discr =
      materializeOperand(discrAttr, builder, op->getLoc(), slots,
                         discrType.value_or(Type()), getStringAttr(op, "span"));
  if (!discr)
    return op->emitError("failed to materialize switch_int discriminator");

  mlir::rust::createOp<rust::mir::TypedSwitchIntOp>(
      builder, op->getLoc(), *discr, targets, getStringAttr(op, "span"));
  return success();
}

LogicalResult lowerAssert(Operation *op, OpBuilder &builder) {
  DictionaryAttr payload = op->getAttrOfType<DictionaryAttr>("payload");
  std::optional<int64_t> target = getInteger(payload, "target");
  if (!target)
    return op->emitError("expected assert target payload");
  mlir::rust::createOp<rust::mir::TypedGotoOp>(
      builder, op->getLoc(), builder.getI64IntegerAttr(*target),
      getStringAttr(op, "span"));
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
            if (failed(lowerAssert(&mirOp, builder)))
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
