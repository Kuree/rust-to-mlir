//===- RustTypedOps.cpp - Typed Rust MIR operations -------------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/RustTyped/IR/RustTypedOps.h"

#include "RustToLLVM/Support/OpCreateCompat.h"
#include "mlir/Dialect/LLVMIR/LLVMTypes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Twine.h"

#include <algorithm>
#include <cassert>
#include <optional>

#define GET_OP_CLASSES
#include "mlir/Dialect/RustTyped/IR/RustTypedOps.cpp.inc"

using namespace mlir;
using namespace mlir::rust::mir;

namespace {
bool isArgumentSlot(LocalSlotOp op) {
  if (std::optional<RustLocalRole> role = op.getRole())
    return *role == RustLocalRole::Arg;
  return false;
}

bool isAddressTaken(LocalSlotOp op) {
  if (BoolAttr attr = op.getAddressTakenAttr())
    return attr.getValue();
  return false;
}

std::optional<int64_t> getConstantIndex(Attribute attr) {
  auto integerAttr = dyn_cast<IntegerAttr>(attr);
  if (!integerAttr)
    return std::nullopt;
  return integerAttr.getInt();
}

SmallVector<Attribute>
getSortedIndices(const llvm::DenseMap<Attribute, Type> &map) {
  SmallVector<Attribute> indices;
  indices.reserve(map.size());
  for (auto [index, type] : map)
    indices.push_back(index);
  llvm::sort(indices, [](Attribute lhs, Attribute rhs) {
    std::optional<int64_t> lhsIndex = getConstantIndex(lhs);
    std::optional<int64_t> rhsIndex = getConstantIndex(rhs);
    assert(lhsIndex && rhsIndex && "expected integer destructurable indices");
    return *lhsIndex < *rhsIndex;
  });
  return indices;
}

SmallVector<Attribute>
getSortedIndices(const llvm::DenseMap<Attribute, MemorySlot> &map) {
  SmallVector<Attribute> indices;
  indices.reserve(map.size());
  for (auto [index, slot] : map)
    indices.push_back(index);
  llvm::sort(indices, [](Attribute lhs, Attribute rhs) {
    std::optional<int64_t> lhsIndex = getConstantIndex(lhs);
    std::optional<int64_t> rhsIndex = getConstantIndex(rhs);
    assert(lhsIndex && rhsIndex && "expected integer destructurable indices");
    return *lhsIndex < *rhsIndex;
  });
  return indices;
}

bool isDestructurableAggregate(Type type) {
  auto destructurable = dyn_cast<DestructurableTypeInterface>(type);
  return destructurable && destructurable.getSubelementIndexMap().has_value();
}

Type getIndexedElementType(Type aggregateType, Attribute index) {
  std::optional<int64_t> constantIndex = getConstantIndex(index);
  if (!constantIndex)
    return {};

  if (auto destructurable =
          dyn_cast<DestructurableTypeInterface>(aggregateType)) {
    if (Type elementType = destructurable.getTypeAtIndex(index))
      return elementType;
  }
  if (auto structType = dyn_cast<LLVM::LLVMStructType>(aggregateType)) {
    if (*constantIndex < 0 ||
        static_cast<size_t>(*constantIndex) >= structType.getBody().size())
      return {};
    return structType.getBody()[*constantIndex];
  }
  if (auto arrayType = dyn_cast<LLVM::LLVMArrayType>(aggregateType)) {
    if (*constantIndex < 0 ||
        static_cast<uint64_t>(*constantIndex) >= arrayType.getNumElements())
      return {};
    return arrayType.getElementType();
  }
  return {};
}

Type getAddressElementType(Type addressType) {
  if (auto slotType = dyn_cast<SlotType>(addressType))
    return slotType.getElementType();
  if (auto addrType = dyn_cast<TypedAddrType>(addressType))
    return addrType.getElementType();
  return {};
}

Value createTypedLoad(Location loc, OpBuilder &builder,
                      const MemorySlot &slot) {
  return mlir::rust::createOp<LoadOp>(builder, loc, slot.elemType, slot.ptr)
      .getValue();
}

void createTypedStore(Location loc, OpBuilder &builder, Value value,
                      const MemorySlot &slot) {
  mlir::rust::createOp<StoreOp>(builder, loc, value, slot.ptr);
}
} // namespace

LogicalResult LoadOp::verify() {
  SlotType slotType = getSlot().getType();
  if (getValue().getType() != slotType.getElementType())
    return emitOpError("result type must match slot element type");
  return success();
}

LogicalResult StoreOp::verify() {
  SlotType slotType = getSlot().getType();
  if (getValue() == getSlot())
    return emitOpError("cannot store a slot into itself");
  if (getValue().getType() != slotType.getElementType())
    return emitOpError("value type must match slot element type");
  return success();
}

LogicalResult FieldAddrOp::verify() {
  Type baseElementType = getAddressElementType(getBase().getType());
  if (!baseElementType)
    return emitOpError("base must be a typed Rust local slot or place "
                       "address");

  TypedAddrType resultType = getAddress().getType();
  Type fieldType = getIndexedElementType(baseElementType, getIndexAttr());
  if (!fieldType)
    return emitOpError("base element type has no field at index ")
           << getIndex();
  if (fieldType != resultType.getElementType())
    return emitOpError("result element type must match projected field type");
  return success();
}

LogicalResult BorrowOp::verify() {
  auto resultType = dyn_cast<TypedRefType>(getResult().getType());
  if (!resultType)
    return emitOpError("result type must be a typed Rust reference");

  Type pointeeType = getAddressElementType(getSlot().getType());
  if (!pointeeType)
    return emitOpError("operand must be a typed Rust local slot or place "
                       "address");
  if (pointeeType != resultType.getPointeeType())
    return emitOpError("operand element type must match reference pointee "
                       "type");
  if (stringifyRustMutability(getMutability()) != resultType.getMutability())
    return emitOpError("mutability attribute must match reference type");
  return success();
}

LogicalResult RawAddressOp::verify() {
  auto resultType = dyn_cast<TypedRawPtrType>(getResult().getType());
  if (!resultType)
    return emitOpError("result type must be a typed Rust raw pointer");

  Type pointeeType = getAddressElementType(getSlot().getType());
  if (!pointeeType)
    return emitOpError("operand must be a typed Rust local slot or place "
                       "address");
  if (pointeeType != resultType.getPointeeType())
    return emitOpError("operand element type must match raw pointer pointee "
                       "type");
  if (stringifyRustMutability(getMutability()) != resultType.getMutability())
    return emitOpError("mutability attribute must match raw pointer type");
  return success();
}

LogicalResult TypedCallOp::verify() {
  if (auto cVariadic = getCVariadicAttr()) {
    if (cVariadic.getValue()) {
      std::optional<RustAbi> abi = getAbi();
      if (!abi || *abi != RustAbi::C)
        return emitOpError("c_variadic calls must use C ABI");
    }
  }

  return success();
}

llvm::SmallVector<MemorySlot> LocalSlotOp::getPromotableSlots() {
  if (isArgumentSlot(*this) || isAddressTaken(*this))
    return {};

  SlotType slotType = getSlot().getType();
  return {MemorySlot{getSlot(), slotType.getElementType()}};
}

Value LocalSlotOp::getDefaultValue(const MemorySlot &slot, OpBuilder &builder) {
  return mlir::rust::createOp<TypedConstOp>(
             builder, getLoc(), slot.elemType, Attribute(),
             builder.getStringAttr("uninit"), StringAttr())
      .getResult();
}

void LocalSlotOp::handleBlockArgument(const MemorySlot &, BlockArgument,
                                      OpBuilder &) {}

std::optional<PromotableAllocationOpInterface>
LocalSlotOp::handlePromotionComplete(const MemorySlot &slot, Value defaultValue,
                                     OpBuilder &) {
  if (Operation *defaultOp =
          defaultValue ? defaultValue.getDefiningOp() : nullptr) {
    if (defaultOp->use_empty() && isa<TypedConstOp>(defaultOp))
      defaultOp->erase();
  }

  if (slot.ptr == getSlot() && getSlot().use_empty())
    erase();

  return std::nullopt;
}

llvm::SmallVector<DestructurableMemorySlot>
LocalSlotOp::getDestructurableSlots() {
  if (isArgumentSlot(*this) || isAddressTaken(*this))
    return {};

  SlotType slotType = getSlot().getType();
  auto destructurable =
      dyn_cast<DestructurableTypeInterface>(slotType.getElementType());
  if (!destructurable)
    return {};

  std::optional<llvm::DenseMap<Attribute, Type>> subelementTypes =
      destructurable.getSubelementIndexMap();
  if (!subelementTypes)
    return {};

  DestructurableMemorySlot slot;
  slot.ptr = getSlot();
  slot.elemType = slotType.getElementType();
  slot.subelementTypes = std::move(*subelementTypes);
  return {slot};
}

llvm::DenseMap<Attribute, MemorySlot> LocalSlotOp::destructure(
    const DestructurableMemorySlot &slot,
    const llvm::SmallPtrSetImpl<Attribute> &usedIndices, OpBuilder &builder,
    SmallVectorImpl<DestructurableAllocationOpInterface> &newAllocators) {
  llvm::DenseMap<Attribute, MemorySlot> subslots;
  for (Attribute index : getSortedIndices(slot.subelementTypes)) {
    if (!usedIndices.count(index))
      continue;

    Type elementType = slot.subelementTypes.lookup(index);
    StringAttr subslotName;
    if (StringAttr name = getNameAttr()) {
      int64_t fieldIndex = *getConstantIndex(index);
      subslotName = builder.getStringAttr(
          (name.getValue() + "." + Twine(fieldIndex)).str());
    }

    auto subslotOp = mlir::rust::createOp<LocalSlotOp>(
        builder, getLoc(), SlotType::get(getContext(), elementType),
        getIndexAttr(), subslotName, getMutabilityAttr(), getRoleAttr(),
        getSpanAttr(), BoolAttr());
    subslots[index] = MemorySlot{subslotOp.getSlot(), elementType};
    if (isDestructurableAggregate(elementType))
      if (auto allocator = dyn_cast<DestructurableAllocationOpInterface>(
              subslotOp.getOperation()))
        newAllocators.push_back(allocator);
  }
  return subslots;
}

std::optional<DestructurableAllocationOpInterface>
LocalSlotOp::handleDestructuringComplete(const DestructurableMemorySlot &slot,
                                         OpBuilder &) {
  if (slot.ptr == getSlot() && getSlot().use_empty())
    erase();
  return std::nullopt;
}

bool LoadOp::loadsFrom(const MemorySlot &slot) { return getSlot() == slot.ptr; }

bool LoadOp::storesTo(const MemorySlot &) { return false; }

Value LoadOp::getStored(const MemorySlot &, OpBuilder &, Value,
                        const DataLayout &) {
  return {};
}

bool LoadOp::canUsesBeRemoved(const MemorySlot &slot,
                              const llvm::SmallPtrSetImpl<OpOperand *> &uses,
                              llvm::SmallVectorImpl<OpOperand *> &,
                              const DataLayout &) {
  return getSlot() == slot.ptr && uses.size() == 1 &&
         uses.count(&getSlotMutable()) && getValue().getType() == slot.elemType;
}

DeletionKind LoadOp::removeBlockingUses(
    const MemorySlot &slot, const llvm::SmallPtrSetImpl<OpOperand *> &,
    OpBuilder &, Value reachingDefinition, const DataLayout &) {
  if (getSlot() != slot.ptr || !reachingDefinition ||
      reachingDefinition.getType() != getValue().getType())
    return DeletionKind::Keep;

  getValue().replaceAllUsesWith(reachingDefinition);
  return DeletionKind::Delete;
}

bool LoadOp::canRewire(const DestructurableMemorySlot &slot,
                       llvm::SmallPtrSetImpl<Attribute> &usedIndices,
                       SmallVectorImpl<MemorySlot> &, const DataLayout &) {
  if (getSlot() != slot.ptr || getValue().getType() != slot.elemType)
    return false;
  for (auto [index, type] : slot.subelementTypes)
    usedIndices.insert(index);
  return !slot.subelementTypes.empty();
}

DeletionKind LoadOp::rewire(const DestructurableMemorySlot &slot,
                            llvm::DenseMap<Attribute, MemorySlot> &subslots,
                            OpBuilder &builder, const DataLayout &) {
  if (getSlot() != slot.ptr)
    return DeletionKind::Keep;

  SmallVector<Value> values;
  for (Attribute index : getSortedIndices(subslots))
    values.push_back(
        createTypedLoad(getLoc(), builder, subslots.lookup(index)));

  Value aggregate = mlir::rust::createOp<MakeAggregateOp>(builder, getLoc(),
                                                          getValue().getType(),
                                                          values, StringAttr())
                        .getResult();
  getValue().replaceAllUsesWith(aggregate);
  return DeletionKind::Delete;
}

bool StoreOp::loadsFrom(const MemorySlot &) { return false; }

bool StoreOp::storesTo(const MemorySlot &slot) { return getSlot() == slot.ptr; }

Value StoreOp::getStored(const MemorySlot &slot, OpBuilder &, Value,
                         const DataLayout &) {
  if (getSlot() != slot.ptr || getValue().getType() != slot.elemType)
    return {};
  return getValue();
}

bool StoreOp::canUsesBeRemoved(const MemorySlot &slot,
                               const llvm::SmallPtrSetImpl<OpOperand *> &uses,
                               llvm::SmallVectorImpl<OpOperand *> &,
                               const DataLayout &) {
  return getSlot() == slot.ptr && uses.size() == 1 &&
         uses.count(&getSlotMutable()) && getValue().getType() == slot.elemType;
}

DeletionKind
StoreOp::removeBlockingUses(const MemorySlot &slot,
                            const llvm::SmallPtrSetImpl<OpOperand *> &,
                            OpBuilder &, Value, const DataLayout &) {
  return getSlot() == slot.ptr ? DeletionKind::Delete : DeletionKind::Keep;
}

bool StoreOp::canRewire(const DestructurableMemorySlot &slot,
                        llvm::SmallPtrSetImpl<Attribute> &usedIndices,
                        SmallVectorImpl<MemorySlot> &, const DataLayout &) {
  if (getSlot() != slot.ptr || getValue().getType() != slot.elemType)
    return false;
  for (auto [index, type] : slot.subelementTypes)
    usedIndices.insert(index);
  return !slot.subelementTypes.empty();
}

DeletionKind StoreOp::rewire(const DestructurableMemorySlot &slot,
                             llvm::DenseMap<Attribute, MemorySlot> &subslots,
                             OpBuilder &builder, const DataLayout &) {
  if (getSlot() != slot.ptr)
    return DeletionKind::Keep;

  for (Attribute index : getSortedIndices(subslots)) {
    MemorySlot subslot = subslots.lookup(index);
    Value field = mlir::rust::createOp<FieldOp>(
                      builder, getLoc(), subslot.elemType, getValue(),
                      cast<IntegerAttr>(index).getInt(), StringAttr())
                      .getResult();
    createTypedStore(getLoc(), builder, field, subslot);
  }
  return DeletionKind::Delete;
}
