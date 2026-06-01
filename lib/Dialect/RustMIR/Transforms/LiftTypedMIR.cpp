//===- LiftTypedMIR.cpp - Lift rust.mir to rust.typed ----------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/RustMIR/Transforms/Passes.h"

#include "RustToMLIR/Support/OpCreateCompat.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
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
#include "mlir/Interfaces/MemorySlotInterfaces.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"

#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <string>

using namespace mlir;

namespace mlir {
#define GEN_PASS_DEF_LIFTTYPEDMIRPASS
#include "mlir/Dialect/RustMIR/Transforms/RustMIRTransformsPasses.h.inc"
} // namespace mlir

namespace {
struct LocalSlot {
  Value slot;
  Type elementType;
};

struct PlaceAddress {
  Value slot;
  Type elementType;
};

struct DropLoweringContext {
  ModuleOp module;
  OpBuilder &builder;
  llvm::StringMap<LocalSlot> &slots;
  Block *typedFuncBlock;
  int64_t &nextSyntheticBlock;
  Operation *diagnosticOp;
};

struct DropCallAttrs {
  IntegerAttr targetAttr;
  rust::mir::RustUnwindActionAttr unwindAttr;
};

struct DropAddress {
  Type elementType;
  std::function<std::optional<Value>(DropLoweringContext &, Location,
                                     StringAttr)>
      materialize;
};

enum class RangeIndexKind {
  Full,
  FromTo,
  From,
  To,
  FromToInclusive,
  ToInclusive,
};

bool isOptionAdtType(Type type) {
  auto adtType = dyn_cast<rust::mir::AdtType>(type);
  if (!adtType || adtType.getVariants().size() != 2)
    return false;
  StringRef name = adtType.getName();
  return name.starts_with("std::option::Option<") ||
         name.starts_with("core::option::Option<") ||
         name == "std::option::Option" || name == "core::option::Option";
}

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

std::optional<std::string>
extractConstantRustName(rust::mir::ConstantOp constant) {
  // A function-item callee has no structural model yet, so its type is carried
  // as an OpaqueType whose spelling is rustc's Debug rendering (e.g. FnDef).
  if (std::optional<Type> ty = constant.getTy())
    if (auto opaque = dyn_cast<rust::mir::OpaqueType>(*ty))
      if (std::optional<std::string> name =
              extractRustDefName(opaque.getSpelling()))
        return name;
  if (std::optional<StringRef> debug = constant.getDebug())
    return extractRustDefName(*debug);
  return std::nullopt;
}

std::optional<std::string> extractCallRustName(rust::mir::CallOp call) {
  if (std::optional<StringRef> calleeName = call.getCalleeName())
    return calleeName->str();

  auto callee = dyn_cast_or_null<rust::mir::ConstantOp>(childAt(call, 0));
  if (!callee)
    return std::nullopt;
  return extractConstantRustName(callee);
}

bool isCAbiCall(rust::mir::CallOp call) {
  if (std::optional<rust::mir::RustAbi> abi = call.getCalleeAbi())
    return *abi == rust::mir::RustAbi::C;
  if (std::optional<std::string> rustName = extractCallRustName(call))
    return StringRef(*rustName).starts_with("__rtl_") ||
           StringRef(*rustName).contains("::__rtl_");
  return false;
}

bool hasUnreachableUnwind(rust::mir::CallOp call) {
  rust::mir::RustUnwindActionAttr unwind = call.getUnwindAttr();
  return unwind &&
         unwind.getValue() == rust::mir::RustUnwindAction::Unreachable;
}

bool isPanicFmtCallName(StringRef rustName) {
  return rustName == "std::rt::panic_fmt" ||
         rustName.ends_with("::panicking::panic_fmt");
}

bool isPanicArgumentsFromStrCallName(StringRef rustName) {
  return rustName.contains("std::fmt::Arguments") &&
         rustName.ends_with("::from_str");
}

bool isClosureTraitCallName(StringRef rustName) {
  return rustName == "std::ops::Fn::call" ||
         rustName == "core::ops::Fn::call" ||
         rustName == "std::ops::FnMut::call_mut" ||
         rustName == "core::ops::FnMut::call_mut" ||
         rustName == "std::ops::FnOnce::call_once" ||
         rustName == "core::ops::FnOnce::call_once";
}

bool isClosureTraitCall(rust::mir::CallOp call) {
  StringAttr calleeDef = call.getCalleeDefAttr();
  return calleeDef && isClosureTraitCallName(calleeDef.getValue());
}

bool isRustIndexCallName(StringRef rustName) {
  return rustName.ends_with("ops::Index::index") ||
         rustName.ends_with("ops::IndexMut::index_mut");
}

bool isRangeIntoIteratorCall(rust::mir::CallOp call, StringRef rustName) {
  if (!call.getRangeKind())
    return false;
  return rustName.ends_with("iter::IntoIterator::into_iter") ||
         rustName.ends_with("iter::traits::collect::IntoIterator::into_iter");
}

bool isRangeInclusiveNewCallName(StringRef rustName) {
  return rustName.contains("ops::RangeInclusive") &&
         rustName.ends_with("::new");
}

std::optional<RangeIndexKind> getRangeIndexKind(rust::mir::CallOp call,
                                                StringRef rustName) {
  if (!isRustIndexCallName(rustName))
    return std::nullopt;

  // The range kind is carried structurally on the call (derived by the
  // extractor from the callee's generic arguments), not parsed from a string.
  std::optional<rust::mir::RustRangeKind> kind = call.getRangeKind();
  if (!kind)
    return std::nullopt;
  switch (*kind) {
  case rust::mir::RustRangeKind::Full:
    return RangeIndexKind::Full;
  case rust::mir::RustRangeKind::FromTo:
    return RangeIndexKind::FromTo;
  case rust::mir::RustRangeKind::From:
    return RangeIndexKind::From;
  case rust::mir::RustRangeKind::To:
    return RangeIndexKind::To;
  case rust::mir::RustRangeKind::FromToInclusive:
    return RangeIndexKind::FromToInclusive;
  case rust::mir::RustRangeKind::ToInclusive:
    return RangeIndexKind::ToInclusive;
  }
  return std::nullopt;
}

std::string getCAbiSymbol(StringRef rustName) {
  std::pair<StringRef, StringRef> split = rustName.rsplit("::");
  if (!split.second.empty())
    return split.second.str();
  return rustName.str();
}

// The readable, path-based typed symbol (e.g. `crate::foo_typed`). The pipeline
// extracts polymorphic function bodies (one per definition, not per
// monomorphization), so a per-instance disambiguator would never agree between
// a generic definition and its concrete call sites. The stable per-instance
// identity (rustc's mangled name) is carried as call/function metadata instead;
// when true monomorphization is added it becomes the disambiguation key here.
std::string getTypedSymbol(StringRef rustName) {
  return (rustName + "_typed").str();
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
  func.walk([&](rust::mir::DropOp drop) {
    markAddressTakenPlace(childAt(drop, 0), locals);
  });
  return locals;
}

llvm::StringSet<> collectPanicAbortFormattingLocals(rust::mir::FuncOp func) {
  llvm::StringSet<> locals;
  func.walk([&](rust::mir::CallOp call) {
    if (!hasUnreachableUnwind(call))
      return;
    std::optional<std::string> rustName = extractCallRustName(call);
    if (!rustName || !isPanicArgumentsFromStrCallName(*rustName))
      return;

    auto destination = dyn_cast_or_null<rust::mir::PlaceOp>(childAt(call, 1));
    if (std::optional<std::string> localName = getPlaceLocalName(destination))
      locals.insert(*localName);
  });
  return locals;
}

LocalSlot *lookupSlot(llvm::StringMap<LocalSlot> &slots, StringRef name) {
  auto it = slots.find(name);
  if (it == slots.end())
    return nullptr;
  return &it->second;
}

Type getIndexedElementType(Type aggregateType, Attribute index,
                           IntegerAttr variantIndex = {}) {
  if (variantIndex) {
    auto adtType = dyn_cast<rust::mir::AdtType>(aggregateType);
    if (!adtType)
      return {};
    int64_t variant = variantIndex.getInt();
    if (variant < 0 ||
        static_cast<size_t>(variant) >= adtType.getVariants().size())
      return {};
    auto tupleType =
        dyn_cast<rust::mir::TypedTupleType>(adtType.getVariants()[variant]);
    if (!tupleType)
      return {};
    return tupleType.getTypeAtIndex(index);
  }

  if (auto destructurable =
          dyn_cast<DestructurableTypeInterface>(aggregateType))
    return destructurable.getTypeAtIndex(index);
  return {};
}

std::optional<uint64_t> getAggregateElementCount(Type aggregateType) {
  if (auto tupleType = dyn_cast<rust::mir::TypedTupleType>(aggregateType))
    return tupleType.getElementTypes().size();
  if (auto arrayType = dyn_cast<rust::mir::TypedArrayType>(aggregateType))
    return arrayType.getLength();

  auto destructurable = dyn_cast<DestructurableTypeInterface>(aggregateType);
  if (!destructurable)
    return std::nullopt;
  std::optional<llvm::DenseMap<Attribute, Type>> elements =
      destructurable.getSubelementIndexMap();
  if (!elements)
    return std::nullopt;
  return elements->size();
}

std::optional<uint64_t> getVariantElementCount(rust::mir::AdtType adtType,
                                               uint64_t variantIndex) {
  ArrayRef<Type> variants = adtType.getVariants();
  if (variantIndex >= variants.size())
    return std::nullopt;
  if (isa<rust::mir::UnitType>(variants[variantIndex]))
    return 0;
  if (auto tupleType =
          dyn_cast<rust::mir::TypedTupleType>(variants[variantIndex]))
    return tupleType.getElementTypes().size();
  return std::nullopt;
}

Type getAggregateElementType(Type aggregateType, uint64_t index,
                             OpBuilder &builder) {
  if (auto arrayType = dyn_cast<rust::mir::TypedArrayType>(aggregateType))
    return arrayType.getElementType();
  return getIndexedElementType(
      aggregateType, builder.getI64IntegerAttr(static_cast<int64_t>(index)));
}

Type getVariantElementType(rust::mir::AdtType adtType, uint64_t variantIndex,
                           uint64_t fieldIndex, OpBuilder &builder) {
  return getIndexedElementType(
      adtType, builder.getI64IntegerAttr(static_cast<int64_t>(fieldIndex)),
      builder.getI64IntegerAttr(static_cast<int64_t>(variantIndex)));
}

bool isSingleVariantAdt(Type type) {
  auto adtType = dyn_cast<rust::mir::AdtType>(type);
  return adtType && adtType.getVariants().size() == 1 &&
         isa<rust::mir::TypedTupleType>(adtType.getVariants().front());
}

bool isRustIntegerCastType(Type type) {
  return isa<rust::mir::IntType, rust::mir::BoolType>(type);
}

bool isRustFloatCastType(Type type) { return isa<rust::mir::FloatType>(type); }

bool isTriviallyDroppableType(Type type) {
  if (isa<rust::mir::UnitType, rust::mir::NeverType, rust::mir::BoolType,
          rust::mir::CharType, rust::mir::IntType, rust::mir::FloatType,
          rust::mir::TypedRefType, rust::mir::TypedRawPtrType>(type))
    return true;

  if (auto tupleType = dyn_cast<rust::mir::TypedTupleType>(type))
    return llvm::all_of(tupleType.getElementTypes(), isTriviallyDroppableType);

  if (auto arrayType = dyn_cast<rust::mir::TypedArrayType>(type))
    return isTriviallyDroppableType(arrayType.getElementType());

  return false;
}

struct LocalDropImpl {
  std::string rustName;
  std::string typedSymbol;
};

std::optional<LocalDropImpl> getLocalDropImpl(Type type, ModuleOp module) {
  auto adtType = dyn_cast<rust::mir::AdtType>(type);
  if (!adtType)
    return std::nullopt;

  std::string rustName = "<";
  rustName += adtType.getName().str();
  rustName += " as std::ops::Drop>::drop";
  if (SymbolTable::lookupSymbolIn(module, rustName))
    return LocalDropImpl{rustName, getTypedSymbol(rustName)};

  std::string typedName = getTypedSymbol(rustName);
  if (SymbolTable::lookupSymbolIn(module, typedName))
    return LocalDropImpl{rustName, typedName};

  return std::nullopt;
}

void createTypedGoto(OpBuilder &builder, Location loc, int64_t target,
                     StringAttr spanAttr) {
  mlir::rust::createOp<rust::mir::TypedGotoOp>(
      builder, loc, builder.getI64IntegerAttr(target), spanAttr);
}

Type getDynamicallyIndexedElementType(Type aggregateType) {
  if (auto arrayType = dyn_cast<rust::mir::TypedArrayType>(aggregateType))
    return arrayType.getElementType();
  if (auto sliceType = dyn_cast<rust::mir::TypedSliceType>(aggregateType))
    return sliceType.getElementType();
  return {};
}

Type getPointeeType(Type pointerType) {
  if (auto refType = dyn_cast<rust::mir::TypedRefType>(pointerType))
    return refType.getPointeeType();
  if (auto rawPtrType = dyn_cast<rust::mir::TypedRawPtrType>(pointerType))
    return rawPtrType.getPointeeType();
  return {};
}

rust::mir::RustMutability getPointerMutability(Type pointerType) {
  if (auto refType = dyn_cast<rust::mir::TypedRefType>(pointerType))
    return refType.getMutability();
  if (auto rawPtrType = dyn_cast<rust::mir::TypedRawPtrType>(pointerType))
    return rawPtrType.getMutability();
  return rust::mir::RustMutability::Shared;
}

Type getSubsliceType(Type aggregateType, MLIRContext *context) {
  Type elementType = getDynamicallyIndexedElementType(aggregateType);
  if (!elementType)
    return {};
  return rust::mir::TypedSliceType::get(context, elementType);
}

IntegerAttr getStaticProjectionIndexAttr(Operation &projectionElem,
                                         Type aggregateType,
                                         OpBuilder &builder) {
  if (auto field = dyn_cast<rust::mir::ProjectionFieldOp>(projectionElem))
    return field.getIndexAttr();

  auto constantIndex =
      dyn_cast<rust::mir::ProjectionConstantIndexOp>(projectionElem);
  if (!constantIndex)
    return {};

  if (!constantIndex.getFromEnd())
    return constantIndex.getOffsetAttr();

  if (auto arrayType = dyn_cast<rust::mir::TypedArrayType>(aggregateType)) {
    int64_t offset = constantIndex.getOffset();
    uint64_t length = arrayType.getLength();
    if (offset < 0 || static_cast<uint64_t>(offset) > length)
      return {};
    return builder.getI64IntegerAttr(static_cast<int64_t>(length) - offset);
  }

  return {};
}

Value createLoad(OpBuilder &builder, Location loc, Value address,
                 Type elementType) {
  return mlir::rust::createOp<rust::mir::LoadOp>(builder, loc, elementType,
                                                 address)
      .getValue();
}

Value createLoad(OpBuilder &builder, Location loc, LocalSlot &slot) {
  return createLoad(builder, loc, slot.slot, slot.elementType);
}

void createStore(OpBuilder &builder, Location loc, Value value, Value address) {
  mlir::rust::createOp<rust::mir::StoreOp>(builder, loc, value, address);
}

std::optional<PlaceAddress>
materializePlaceAddress(rust::mir::PlaceOp place, OpBuilder &builder,
                        Location loc, llvm::StringMap<LocalSlot> &slots,
                        StringAttr spanAttr) {
  std::optional<std::string> localName = getPlaceLocalName(place);
  if (!localName)
    return std::nullopt;

  LocalSlot *slot = lookupSlot(slots, *localName);
  if (!slot)
    return std::nullopt;

  Value address = slot->slot;
  Type elementType = slot->elementType;
  if (!hasProjection(place))
    return PlaceAddress{address, elementType};

  IntegerAttr variantIndexAttr;
  for (Operation &projectionElem : place.getBody().front()) {
    if (auto downcast =
            dyn_cast<rust::mir::ProjectionDowncastOp>(projectionElem)) {
      variantIndexAttr = downcast.getVariantIndexAttr();
      continue;
    }

    if (isa<rust::mir::ProjectionDerefOp>(projectionElem)) {
      Type pointeeType = getPointeeType(elementType);
      if (!pointeeType)
        return std::nullopt;

      address = createLoad(builder, loc, address, elementType);
      elementType = pointeeType;
      continue;
    }

    if (auto dynamicIndex =
            dyn_cast<rust::mir::ProjectionIndexOp>(projectionElem)) {
      Type indexedType = getDynamicallyIndexedElementType(elementType);
      if (!indexedType)
        return std::nullopt;

      LocalSlot *indexSlot =
          lookupSlot(slots, "_" + std::to_string(dynamicIndex.getLocal()));
      if (!indexSlot)
        return std::nullopt;

      Value index = createLoad(builder, loc, *indexSlot);
      auto addrType =
          rust::mir::TypedAddrType::get(place.getContext(), indexedType);
      address = mlir::rust::createOp<rust::mir::IndexAddrOp>(
                    builder, loc, addrType, address, index, spanAttr)
                    .getAddress();
      elementType = indexedType;
      continue;
    }

    if (auto subslice =
            dyn_cast<rust::mir::ProjectionSubsliceOp>(projectionElem)) {
      Type sliceType = getSubsliceType(elementType, place.getContext());
      if (!sliceType)
        return std::nullopt;

      auto resultType = rust::mir::TypedRefType::get(
          place.getContext(), getPointerMutability(address.getType()),
          sliceType);
      address =
          mlir::rust::createOp<rust::mir::SubsliceOp>(
              builder, loc, resultType, address, subslice.getFromIndexAttr(),
              subslice.getToIndexAttr(), subslice.getFromEndAttr(), spanAttr)
              .getResult();
      elementType = sliceType;
      continue;
    }

    IntegerAttr indexAttr =
        getStaticProjectionIndexAttr(projectionElem, elementType, builder);
    if (!indexAttr)
      return std::nullopt;

    Type fieldType =
        getIndexedElementType(elementType, indexAttr, variantIndexAttr);
    if (!fieldType)
      return std::nullopt;

    auto addrType =
        rust::mir::TypedAddrType::get(place.getContext(), fieldType);
    address = mlir::rust::createOp<rust::mir::FieldAddrOp>(
                  builder, loc, addrType, address, indexAttr, variantIndexAttr,
                  spanAttr)
                  .getAddress();
    elementType = fieldType;
    variantIndexAttr = {};
  }

  return PlaceAddress{address, elementType};
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

  IntegerAttr variantIndexAttr;
  for (Operation &projectionElem : place.getBody().front()) {
    if (auto downcast =
            dyn_cast<rust::mir::ProjectionDowncastOp>(projectionElem)) {
      variantIndexAttr = downcast.getVariantIndexAttr();
      continue;
    }

    if (isa<rust::mir::ProjectionDerefOp>(projectionElem)) {
      type = getPointeeType(type);
      if (!type)
        return std::nullopt;
      continue;
    }

    if (isa<rust::mir::ProjectionIndexOp>(projectionElem)) {
      type = getDynamicallyIndexedElementType(type);
      if (!type)
        return std::nullopt;
      continue;
    }

    if (isa<rust::mir::ProjectionSubsliceOp>(projectionElem)) {
      type = getSubsliceType(type, place.getContext());
      if (!type)
        return std::nullopt;
      continue;
    }

    OpBuilder builder(place.getContext());
    IntegerAttr indexAttr =
        getStaticProjectionIndexAttr(projectionElem, type, builder);
    if (!indexAttr)
      return std::nullopt;

    type = getIndexedElementType(type, indexAttr, variantIndexAttr);
    if (!type)
      return std::nullopt;
    variantIndexAttr = {};
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
    if (std::optional<Type> ty = constant.getTy())
      return *ty;

  return std::nullopt;
}

std::optional<Value> materializePlaceRead(rust::mir::PlaceOp place,
                                          OpBuilder &builder, Location loc,
                                          llvm::StringMap<LocalSlot> &slots,
                                          Type expectedType,
                                          StringAttr spanAttr) {
  std::optional<PlaceAddress> address =
      materializePlaceAddress(place, builder, loc, slots, spanAttr);
  if (!address)
    return std::nullopt;
  Type elementType = address->elementType ? address->elementType : expectedType;
  if (!elementType)
    return std::nullopt;
  return createLoad(builder, loc, address->slot, elementType);
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
      if (std::optional<Type> ty = constant.getTy())
        expectedType = *ty;
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

Value createTypedIntegerConst(OpBuilder &builder, Location loc, Type type,
                              int64_t value, StringAttr spanAttr) {
  return mlir::rust::createOp<rust::mir::TypedConstOp>(
             builder, loc, type, builder.getI64IntegerAttr(value),
             builder.getStringAttr(std::to_string(value)), spanAttr)
      .getResult();
}

std::optional<Value> createTypedBinaryOp(OpBuilder &builder, Location loc,
                                         Type resultType, Value lhs, Value rhs,
                                         rust::mir::RustBinaryOpKind op,
                                         StringAttr spanAttr) {
  if (op == rust::mir::RustBinaryOpKind::Add)
    return mlir::rust::createOp<rust::mir::AddOp>(builder, loc, resultType, lhs,
                                                  rhs, spanAttr)
        .getResult();
  if (op == rust::mir::RustBinaryOpKind::AddUnchecked)
    return mlir::rust::createOp<rust::mir::AddUncheckedOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == rust::mir::RustBinaryOpKind::Sub)
    return mlir::rust::createOp<rust::mir::SubOp>(builder, loc, resultType, lhs,
                                                  rhs, spanAttr)
        .getResult();
  if (op == rust::mir::RustBinaryOpKind::SubUnchecked)
    return mlir::rust::createOp<rust::mir::SubUncheckedOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == rust::mir::RustBinaryOpKind::Mul)
    return mlir::rust::createOp<rust::mir::MulOp>(builder, loc, resultType, lhs,
                                                  rhs, spanAttr)
        .getResult();
  if (op == rust::mir::RustBinaryOpKind::MulUnchecked)
    return mlir::rust::createOp<rust::mir::MulUncheckedOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == rust::mir::RustBinaryOpKind::Div)
    return mlir::rust::createOp<rust::mir::DivOp>(builder, loc, resultType, lhs,
                                                  rhs, spanAttr)
        .getResult();
  if (op == rust::mir::RustBinaryOpKind::Rem)
    return mlir::rust::createOp<rust::mir::RemOp>(builder, loc, resultType, lhs,
                                                  rhs, spanAttr)
        .getResult();
  if (op == rust::mir::RustBinaryOpKind::BitAnd)
    return mlir::rust::createOp<rust::mir::BitAndOp>(builder, loc, resultType,
                                                     lhs, rhs, spanAttr)
        .getResult();
  if (op == rust::mir::RustBinaryOpKind::BitOr)
    return mlir::rust::createOp<rust::mir::BitOrOp>(builder, loc, resultType,
                                                    lhs, rhs, spanAttr)
        .getResult();
  if (op == rust::mir::RustBinaryOpKind::BitXor)
    return mlir::rust::createOp<rust::mir::BitXorOp>(builder, loc, resultType,
                                                     lhs, rhs, spanAttr)
        .getResult();
  if (op == rust::mir::RustBinaryOpKind::Shl)
    return mlir::rust::createOp<rust::mir::ShlOp>(builder, loc, resultType, lhs,
                                                  rhs, spanAttr)
        .getResult();
  if (op == rust::mir::RustBinaryOpKind::ShlUnchecked)
    return mlir::rust::createOp<rust::mir::ShlUncheckedOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == rust::mir::RustBinaryOpKind::Shr)
    return mlir::rust::createOp<rust::mir::ShrOp>(builder, loc, resultType, lhs,
                                                  rhs, spanAttr)
        .getResult();
  if (op == rust::mir::RustBinaryOpKind::ShrUnchecked)
    return mlir::rust::createOp<rust::mir::ShrUncheckedOp>(
               builder, loc, resultType, lhs, rhs, spanAttr)
        .getResult();
  if (op == rust::mir::RustBinaryOpKind::Eq)
    return mlir::rust::createOp<rust::mir::EqOp>(builder, loc, resultType, lhs,
                                                 rhs, spanAttr)
        .getResult();
  if (op == rust::mir::RustBinaryOpKind::Ne)
    return mlir::rust::createOp<rust::mir::NeOp>(builder, loc, resultType, lhs,
                                                 rhs, spanAttr)
        .getResult();
  if (op == rust::mir::RustBinaryOpKind::Lt)
    return mlir::rust::createOp<rust::mir::LtOp>(builder, loc, resultType, lhs,
                                                 rhs, spanAttr)
        .getResult();
  if (op == rust::mir::RustBinaryOpKind::Le)
    return mlir::rust::createOp<rust::mir::LeOp>(builder, loc, resultType, lhs,
                                                 rhs, spanAttr)
        .getResult();
  if (op == rust::mir::RustBinaryOpKind::Gt)
    return mlir::rust::createOp<rust::mir::GtOp>(builder, loc, resultType, lhs,
                                                 rhs, spanAttr)
        .getResult();
  if (op == rust::mir::RustBinaryOpKind::Ge)
    return mlir::rust::createOp<rust::mir::GeOp>(builder, loc, resultType, lhs,
                                                 rhs, spanAttr)
        .getResult();

  return std::nullopt;
}

std::optional<Value> createTypedUnaryOp(OpBuilder &builder, Location loc,
                                        Type resultType, Value input,
                                        rust::mir::RustUnaryOpKind op,
                                        StringAttr spanAttr) {
  if (op == rust::mir::RustUnaryOpKind::Neg)
    return mlir::rust::createOp<rust::mir::NegOp>(builder, loc, resultType,
                                                  input, spanAttr)
        .getResult();
  if (op == rust::mir::RustUnaryOpKind::Not)
    return mlir::rust::createOp<rust::mir::NotOp>(builder, loc, resultType,
                                                  input, spanAttr)
        .getResult();
  if (op == rust::mir::RustUnaryOpKind::PtrMetadata)
    return mlir::rust::createOp<rust::mir::PtrMetadataOp>(
               builder, loc, resultType, input, spanAttr)
        .getMetadata();
  return std::nullopt;
}

std::optional<Value> createCheckedRangeCompare(OpBuilder &builder, Location loc,
                                               Value lhs, Value rhs,
                                               rust::mir::RustBinaryOpKind op,
                                               StringRef message,
                                               StringAttr spanAttr) {
  Type boolType = rust::mir::BoolType::get(builder.getContext());
  std::optional<Value> condition =
      createTypedBinaryOp(builder, loc, boolType, lhs, rhs, op, spanAttr);
  if (!condition)
    return std::nullopt;
  mlir::rust::createOp<rust::mir::TypedAssertOp>(
      builder, loc, *condition, builder.getStringAttr(message),
      rust::mir::RustUnwindActionAttr(), spanAttr);
  return *condition;
}

std::optional<Value> createRangeField(OpBuilder &builder, Location loc,
                                      Value range, uint64_t index,
                                      Type indexType, StringAttr spanAttr) {
  auto tupleType = dyn_cast<rust::mir::TypedTupleType>(range.getType());
  if (!tupleType)
    return std::nullopt;
  Type fieldType = tupleType.getTypeAtIndex(builder.getI64IntegerAttr(index));
  if (!fieldType || fieldType != indexType)
    return std::nullopt;
  return mlir::rust::createOp<rust::mir::FieldOp>(
             builder, loc, indexType, range,
             builder.getI64IntegerAttr(static_cast<int64_t>(index)),
             IntegerAttr(), spanAttr)
      .getResult();
}

std::optional<Value> createIndexArithmetic(OpBuilder &builder, Location loc,
                                           Type indexType, Value lhs, Value rhs,
                                           rust::mir::RustBinaryOpKind op,
                                           StringAttr spanAttr) {
  return createTypedBinaryOp(builder, loc, indexType, lhs, rhs, op, spanAttr);
}

LogicalResult lowerRangeInclusiveNewCall(rust::mir::CallOp op,
                                         OpBuilder &builder,
                                         llvm::StringMap<LocalSlot> &slots,
                                         rust::mir::PlaceOp destination,
                                         uint64_t target) {
  Location loc = op.getLoc();
  StringAttr spanAttr = op.getSpanAttr();
  std::optional<PlaceAddress> dest =
      materializePlaceAddress(destination, builder, loc, slots, spanAttr);
  if (!dest)
    return op.emitError("failed to materialize range destination");

  auto rangeType = dyn_cast<rust::mir::TypedTupleType>(dest->elementType);
  if (!rangeType || rangeType.getElementTypes().size() != 2)
    return op.emitError("range inclusive destination is not a two-field range");

  Operation *startOp = childAt(op, 2);
  Operation *endOp = childAt(op, 3);
  if (!startOp || !endOp)
    return op.emitError("expected range inclusive constructor bounds");

  Type startType = rangeType.getTypeAtIndex(builder.getI64IntegerAttr(0));
  Type endType = rangeType.getTypeAtIndex(builder.getI64IntegerAttr(1));
  std::optional<Value> start =
      materializeOperand(startOp, builder, loc, slots, startType, spanAttr);
  std::optional<Value> end =
      materializeOperand(endOp, builder, loc, slots, endType, spanAttr);
  if (!start || !end)
    return op.emitError("failed to materialize range inclusive bounds");

  Value range = mlir::rust::createOp<rust::mir::MakeAggregateOp>(
                    builder, loc, dest->elementType, ValueRange{*start, *end},
                    IntegerAttr(), StringAttr(), spanAttr)
                    .getResult();
  createStore(builder, loc, range, dest->slot);
  mlir::rust::createOp<rust::mir::TypedGotoOp>(
      builder, loc, builder.getI64IntegerAttr(static_cast<int64_t>(target)),
      spanAttr);
  return success();
}

LogicalResult lowerRangeIndexCall(rust::mir::CallOp op, OpBuilder &builder,
                                  llvm::StringMap<LocalSlot> &slots,
                                  rust::mir::PlaceOp destination,
                                  uint64_t target, RangeIndexKind kind) {
  Location loc = op.getLoc();
  StringAttr spanAttr = op.getSpanAttr();
  Operation *sourceOp = childAt(op, 2);
  Operation *rangeOp = childAt(op, 3);
  if (!sourceOp || !rangeOp)
    return op.emitError("expected range index receiver and bounds");

  std::optional<Type> destinationType = inferPlaceType(destination, slots);
  if (!destinationType)
    return op.emitError("failed to infer range index destination type");
  Type destinationPointee = getPointeeType(*destinationType);
  auto destinationSlice =
      dyn_cast_or_null<rust::mir::TypedSliceType>(destinationPointee);
  if (!destinationSlice)
    return op.emitError("range index destination is not a slice reference");

  std::optional<Type> sourceType = inferOperandType(sourceOp, slots);
  std::optional<Value> source = materializeOperand(
      sourceOp, builder, loc, slots, sourceType.value_or(Type()), spanAttr);
  if (!source)
    return op.emitError("failed to materialize range index receiver");

  Type sourcePointee = getPointeeType((*source).getType());
  Type sourceElementType = getDynamicallyIndexedElementType(sourcePointee);
  if (!sourceElementType)
    return op.emitError("range index receiver is not sliceable");
  if (sourceElementType != destinationSlice.getElementType())
    return op.emitError("range index result element type does not match "
                        "receiver element type");

  MLIRContext *context = op.getContext();
  Type indexType = rust::mir::IntType::getFromSpelling(context, "usize");
  Value zero = createTypedIntegerConst(builder, loc, indexType, 0, spanAttr);
  Value one = createTypedIntegerConst(builder, loc, indexType, 1, spanAttr);
  Value sourceLen = mlir::rust::createOp<rust::mir::PtrMetadataOp>(
                        builder, loc, indexType, *source, spanAttr)
                        .getMetadata();

  Value start = zero;
  Value length = sourceLen;
  std::optional<Value> range;
  auto materializeRange = [&]() -> std::optional<Value> {
    if (range)
      return range;
    std::optional<Type> rangeType = inferOperandType(rangeOp, slots);
    if (!rangeType)
      return std::nullopt;
    range =
        materializeOperand(rangeOp, builder, loc, slots, *rangeType, spanAttr);
    return range;
  };

  auto setEndExclusive = [&](Value end) -> LogicalResult {
    if (!createCheckedRangeCompare(builder, loc, start, end,
                                   rust::mir::RustBinaryOpKind::Le,
                                   "slice index starts after end", spanAttr))
      return failure();
    if (!createCheckedRangeCompare(
            builder, loc, end, sourceLen, rust::mir::RustBinaryOpKind::Le,
            "range end index out of range for slice", spanAttr))
      return failure();
    std::optional<Value> computedLength =
        createIndexArithmetic(builder, loc, indexType, end, start,
                              rust::mir::RustBinaryOpKind::Sub, spanAttr);
    if (!computedLength)
      return failure();
    length = *computedLength;
    return success();
  };

  switch (kind) {
  case RangeIndexKind::Full:
    break;
  case RangeIndexKind::FromTo: {
    std::optional<Value> rangeValue = materializeRange();
    if (!rangeValue)
      return op.emitError("failed to materialize range bounds");
    std::optional<Value> rangeStart =
        createRangeField(builder, loc, *rangeValue, 0, indexType, spanAttr);
    std::optional<Value> rangeEnd =
        createRangeField(builder, loc, *rangeValue, 1, indexType, spanAttr);
    if (!rangeStart || !rangeEnd)
      return op.emitError("failed to extract range bounds");
    start = *rangeStart;
    if (failed(setEndExclusive(*rangeEnd)))
      return op.emitError("failed to lower range bounds");
    break;
  }
  case RangeIndexKind::From: {
    std::optional<Value> rangeValue = materializeRange();
    if (!rangeValue)
      return op.emitError("failed to materialize range start");
    std::optional<Value> rangeStart =
        createRangeField(builder, loc, *rangeValue, 0, indexType, spanAttr);
    if (!rangeStart)
      return op.emitError("failed to extract range start");
    start = *rangeStart;
    if (!createCheckedRangeCompare(
            builder, loc, start, sourceLen, rust::mir::RustBinaryOpKind::Le,
            "range start index out of range for slice", spanAttr))
      return op.emitError("failed to lower range start bounds check");
    std::optional<Value> computedLength =
        createIndexArithmetic(builder, loc, indexType, sourceLen, start,
                              rust::mir::RustBinaryOpKind::Sub, spanAttr);
    if (!computedLength)
      return op.emitError("failed to compute range length");
    length = *computedLength;
    break;
  }
  case RangeIndexKind::To: {
    std::optional<Value> rangeValue = materializeRange();
    if (!rangeValue)
      return op.emitError("failed to materialize range end");
    std::optional<Value> end =
        createRangeField(builder, loc, *rangeValue, 0, indexType, spanAttr);
    if (!end)
      return op.emitError("failed to extract range end");
    if (!createCheckedRangeCompare(
            builder, loc, *end, sourceLen, rust::mir::RustBinaryOpKind::Le,
            "range end index out of range for slice", spanAttr))
      return op.emitError("failed to lower range end bounds check");
    length = *end;
    break;
  }
  case RangeIndexKind::FromToInclusive: {
    std::optional<Value> rangeValue = materializeRange();
    if (!rangeValue)
      return op.emitError("failed to materialize inclusive range bounds");
    std::optional<Value> rangeStart =
        createRangeField(builder, loc, *rangeValue, 0, indexType, spanAttr);
    std::optional<Value> rangeEnd =
        createRangeField(builder, loc, *rangeValue, 1, indexType, spanAttr);
    if (!rangeStart || !rangeEnd)
      return op.emitError("failed to extract inclusive range bounds");
    start = *rangeStart;
    if (!createCheckedRangeCompare(builder, loc, start, *rangeEnd,
                                   rust::mir::RustBinaryOpKind::Le,
                                   "slice index starts after end", spanAttr))
      return op.emitError("failed to lower inclusive range order check");
    if (!createCheckedRangeCompare(
            builder, loc, *rangeEnd, sourceLen, rust::mir::RustBinaryOpKind::Lt,
            "range end index out of range for slice", spanAttr))
      return op.emitError("failed to lower inclusive range bounds check");
    std::optional<Value> endDelta =
        createIndexArithmetic(builder, loc, indexType, *rangeEnd, start,
                              rust::mir::RustBinaryOpKind::Sub, spanAttr);
    if (!endDelta)
      return op.emitError("failed to compute inclusive range delta");
    std::optional<Value> computedLength =
        createIndexArithmetic(builder, loc, indexType, *endDelta, one,
                              rust::mir::RustBinaryOpKind::Add, spanAttr);
    if (!computedLength)
      return op.emitError("failed to compute inclusive range length");
    length = *computedLength;
    break;
  }
  case RangeIndexKind::ToInclusive: {
    std::optional<Value> rangeValue = materializeRange();
    if (!rangeValue)
      return op.emitError("failed to materialize inclusive range end");
    std::optional<Value> end =
        createRangeField(builder, loc, *rangeValue, 0, indexType, spanAttr);
    if (!end)
      return op.emitError("failed to extract inclusive range end");
    if (!createCheckedRangeCompare(
            builder, loc, *end, sourceLen, rust::mir::RustBinaryOpKind::Lt,
            "range end index out of range for slice", spanAttr))
      return op.emitError("failed to lower inclusive range end bounds check");
    std::optional<Value> computedLength =
        createIndexArithmetic(builder, loc, indexType, *end, one,
                              rust::mir::RustBinaryOpKind::Add, spanAttr);
    if (!computedLength)
      return op.emitError("failed to compute inclusive range length");
    length = *computedLength;
    break;
  }
  }

  Value result =
      mlir::rust::createOp<rust::mir::SliceRangeOp>(
          builder, loc, *destinationType, *source, start, length, spanAttr)
          .getResult();
  std::optional<PlaceAddress> dest =
      materializePlaceAddress(destination, builder, loc, slots, spanAttr);
  if (!dest)
    return op.emitError("failed to materialize range index destination");
  createStore(builder, loc, result, dest->slot);
  mlir::rust::createOp<rust::mir::TypedGotoOp>(
      builder, loc, builder.getI64IntegerAttr(static_cast<int64_t>(target)),
      spanAttr);
  return success();
}

LogicalResult lowerRangeIntoIteratorCall(rust::mir::CallOp op,
                                         OpBuilder &builder,
                                         llvm::StringMap<LocalSlot> &slots,
                                         rust::mir::PlaceOp destination,
                                         uint64_t target) {
  Location loc = op.getLoc();
  StringAttr spanAttr = op.getSpanAttr();
  Operation *rangeOp = childAt(op, 2);
  if (!rangeOp)
    return op.emitError("expected range into_iter argument");

  std::optional<Type> destinationType = inferPlaceType(destination, slots);
  if (!destinationType)
    return op.emitError("failed to infer range into_iter destination type");

  std::optional<Value> range = materializeOperand(rangeOp, builder, loc, slots,
                                                  *destinationType, spanAttr);
  if (!range)
    return op.emitError("failed to materialize range into_iter argument");

  std::optional<PlaceAddress> dest =
      materializePlaceAddress(destination, builder, loc, slots, spanAttr);
  if (!dest)
    return op.emitError("failed to materialize range into_iter destination");

  createStore(builder, loc, *range, dest->slot);
  mlir::rust::createOp<rust::mir::TypedGotoOp>(
      builder, loc, builder.getI64IntegerAttr(static_cast<int64_t>(target)),
      spanAttr);
  return success();
}

LogicalResult lowerAssign(rust::mir::AssignOp assign, OpBuilder &builder,
                          llvm::StringMap<LocalSlot> &slots) {
  auto place = dyn_cast_or_null<rust::mir::PlaceOp>(childAt(assign, 0));
  if (!place)
    return assign.emitError("expected assignment destination place");

  std::optional<PlaceAddress> dest = materializePlaceAddress(
      place, builder, assign.getLoc(), slots, assign.getSpanAttr());
  if (!dest)
    return assign.emitError("failed to materialize assignment destination");

  Operation *rvalue = childAt(assign, 1);
  if (!rvalue)
    return assign.emitError("expected assignment rvalue operation");

  auto lowerBinaryRvalue = [&](auto binaryRvalue,
                               rust::mir::RustBinaryOpKind op,
                               bool isChecked) -> LogicalResult {
    Operation *lhsOp = childAt(binaryRvalue, 0);
    Operation *rhsOp = childAt(binaryRvalue, 1);
    if (!lhsOp || !rhsOp)
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
      if (op == rust::mir::RustBinaryOpKind::Add) {
        auto checked = mlir::rust::createOp<rust::mir::CheckedAddOp>(
            builder, loc, resultType, overflowType, *lhs, *rhs, spanAttr);
        value = checked.getValue();
        overflow = checked.getOverflow();
      } else if (op == rust::mir::RustBinaryOpKind::Sub) {
        auto checked = mlir::rust::createOp<rust::mir::CheckedSubOp>(
            builder, loc, resultType, overflowType, *lhs, *rhs, spanAttr);
        value = checked.getValue();
        overflow = checked.getOverflow();
      } else if (op == rust::mir::RustBinaryOpKind::Mul) {
        auto checked = mlir::rust::createOp<rust::mir::CheckedMulOp>(
            builder, loc, resultType, overflowType, *lhs, *rhs, spanAttr);
        value = checked.getValue();
        overflow = checked.getOverflow();
      } else {
        return assign.emitError("unsupported checked binary op: ")
               << rust::mir::stringifyRustBinaryOpKind(op);
      }

      Value tuple =
          mlir::rust::createOp<rust::mir::MakeAggregateOp>(
              builder, loc, dest->elementType, ValueRange{value, overflow},
              IntegerAttr(), StringAttr(), spanAttr)
              .getResult();
      createStore(builder, loc, tuple, dest->slot);
      return success();
    }

    std::optional<Value> result =
        createTypedBinaryOp(builder, loc, resultType, *lhs, *rhs, op, spanAttr);
    if (!result)
      return assign.emitError("unsupported binary op: ")
             << rust::mir::stringifyRustBinaryOpKind(op);
    createStore(builder, loc, *result, dest->slot);
    return success();
  };

  if (auto binary = dyn_cast<rust::mir::BinaryOp>(rvalue))
    return lowerBinaryRvalue(binary, binary.getOp(), false);
  if (auto checked = dyn_cast<rust::mir::CheckedBinaryOp>(rvalue))
    return lowerBinaryRvalue(checked, checked.getOp(), true);

  if (auto unary = dyn_cast<rust::mir::UnaryOp>(rvalue)) {
    rust::mir::RustUnaryOpKind op = unary.getOp();
    Operation *operandOp = childAt(unary, 0);
    if (!operandOp)
      return assign.emitError("expected unary operation operand");

    std::optional<Value> operand =
        materializeOperand(operandOp, builder, assign.getLoc(), slots,
                           dest->elementType, assign.getSpanAttr());
    if (!operand)
      return assign.emitError("failed to materialize unary operand");

    std::optional<Value> result =
        createTypedUnaryOp(builder, assign.getLoc(), dest->elementType,
                           *operand, op, assign.getSpanAttr());
    if (!result)
      return assign.emitError("unsupported unary op: ")
             << rust::mir::stringifyRustUnaryOpKind(op);
    createStore(builder, assign.getLoc(), *result, dest->slot);
    return success();
  }

  if (auto cast = dyn_cast<rust::mir::CastOp>(rvalue)) {
    Operation *operandOp = childAt(cast, 0);
    if (!operandOp)
      return assign.emitError("expected cast operand");

    if (cast.getCastKind() == rust::mir::RustCastKind::PointerCoercion) {
      if (auto fnType = dyn_cast<FunctionType>(dest->elementType)) {
        auto constant = dyn_cast_or_null<rust::mir::ConstantOp>(operandOp);
        if (!constant)
          return assign.emitError(
              "expected function item constant for fn pointer coercion");
        std::optional<std::string> rustName = extractConstantRustName(constant);
        if (!rustName)
          return assign.emitError(
              "failed to extract function name for fn pointer coercion");

        Value result =
            mlir::rust::createOp<rust::mir::FnAddrOp>(
                builder, assign.getLoc(), fnType,
                FlatSymbolRefAttr::get(assign.getContext(),
                                       getTypedSymbol(*rustName)),
                builder.getStringAttr(*rustName), assign.getSpanAttr())
                .getResult();
        createStore(builder, assign.getLoc(), result, dest->slot);
        return success();
      }
    }

    Type operandExpectedType =
        inferOperandType(operandOp, slots).value_or(Type());
    std::optional<Value> operand =
        materializeOperand(operandOp, builder, assign.getLoc(), slots,
                           operandExpectedType, assign.getSpanAttr());
    if (!operand)
      return assign.emitError("failed to materialize cast operand");

    if (cast.getCastKind() == rust::mir::RustCastKind::IntToInt) {
      if (!isRustIntegerCastType((*operand).getType()) ||
          !isRustIntegerCastType(dest->elementType))
        return assign.emitError("unsupported int-to-int cast types");

      if ((*operand).getType() == dest->elementType) {
        createStore(builder, assign.getLoc(), *operand, dest->slot);
        return success();
      }

      Value result = mlir::rust::createOp<rust::mir::IntCastOp>(
                         builder, assign.getLoc(), dest->elementType, *operand,
                         assign.getSpanAttr())
                         .getResult();
      createStore(builder, assign.getLoc(), result, dest->slot);
      return success();
    }

    if (cast.getCastKind() == rust::mir::RustCastKind::FloatToInt ||
        cast.getCastKind() == rust::mir::RustCastKind::FloatToFloat ||
        cast.getCastKind() == rust::mir::RustCastKind::IntToFloat) {
      bool validNumericCast =
          (cast.getCastKind() == rust::mir::RustCastKind::FloatToInt &&
           isRustFloatCastType((*operand).getType()) &&
           isRustIntegerCastType(dest->elementType)) ||
          (cast.getCastKind() == rust::mir::RustCastKind::FloatToFloat &&
           isRustFloatCastType((*operand).getType()) &&
           isRustFloatCastType(dest->elementType)) ||
          (cast.getCastKind() == rust::mir::RustCastKind::IntToFloat &&
           isRustIntegerCastType((*operand).getType()) &&
           isRustFloatCastType(dest->elementType));
      if (!validNumericCast)
        return assign.emitError("unsupported numeric cast types");

      Value result = mlir::rust::createOp<rust::mir::NumericCastOp>(
                         builder, assign.getLoc(), dest->elementType,
                         cast.getCastKindAttr(), *operand, assign.getSpanAttr())
                         .getResult();
      createStore(builder, assign.getLoc(), result, dest->slot);
      return success();
    }

    if (cast.getCastKind() == rust::mir::RustCastKind::PointerCoercion) {
      Type sourcePointee = getPointeeType((*operand).getType());
      Type destPointee = getPointeeType(dest->elementType);
      auto sourceArray =
          dyn_cast_or_null<rust::mir::TypedArrayType>(sourcePointee);
      auto destSlice = dyn_cast_or_null<rust::mir::TypedSliceType>(destPointee);
      if (sourceArray && destSlice &&
          sourceArray.getElementType() == destSlice.getElementType()) {
        Value result = mlir::rust::createOp<rust::mir::SliceFromArrayOp>(
                           builder, assign.getLoc(), dest->elementType,
                           *operand, assign.getSpanAttr())
                           .getResult();
        createStore(builder, assign.getLoc(), result, dest->slot);
        return success();
      }
    }

    if ((*operand).getType() == dest->elementType) {
      createStore(builder, assign.getLoc(), *operand, dest->slot);
      return success();
    }

    return assign.emitError("unsupported cast rvalue");
  }

  if (auto aggregate = dyn_cast<rust::mir::AggregateOp>(rvalue)) {
    rust::mir::RustAggregateKind aggregateKind = aggregate.getAggregateKind();
    if ((aggregateKind != rust::mir::RustAggregateKind::Tuple &&
         aggregateKind != rust::mir::RustAggregateKind::Array &&
         aggregateKind != rust::mir::RustAggregateKind::Adt &&
             aggregateKind != rust::mir::RustAggregateKind::Closure) ||
        aggregate.getBody().empty())
      return assign.emitError("only tuple, array, ADT, and closure aggregate "
                              "rvalues can be lifted");

    auto tupleType = dyn_cast<rust::mir::TypedTupleType>(dest->elementType);
    auto arrayType = dyn_cast<rust::mir::TypedArrayType>(dest->elementType);
    auto adtType = dyn_cast<rust::mir::AdtType>(dest->elementType);
    bool closureAggregate =
        aggregateKind == rust::mir::RustAggregateKind::Closure;
    bool singleVariantAdt = isSingleVariantAdt(dest->elementType);
    if (aggregateKind == rust::mir::RustAggregateKind::Tuple && !tupleType)
      return assign.emitError("tuple aggregate destination is not a tuple");
    if (aggregateKind == rust::mir::RustAggregateKind::Array && !arrayType)
      return assign.emitError("array aggregate destination is not an array");
    if (aggregateKind == rust::mir::RustAggregateKind::Adt && !tupleType &&
        !singleVariantAdt && !adtType)
      return assign.emitError("ADT aggregate destination is not an ADT");
    if (closureAggregate && !tupleType &&
        !isa<rust::mir::UnitType>(dest->elementType))
      return assign.emitError(
          "closure aggregate destination is not a capture tuple");
    if (closureAggregate && isa<rust::mir::UnitType>(dest->elementType)) {
      if (!aggregate.getBody().front().empty())
        return assign.emitError(
            "unit closure aggregate cannot have capture operands");
      return success();
    }
    IntegerAttr variantIndexAttr = aggregate.getVariantIndexAttr();
    if (aggregateKind == rust::mir::RustAggregateKind::Adt && adtType &&
        adtType.getVariants().size() > 1 && !variantIndexAttr)
      return assign.emitError("ADT enum aggregate is missing variant index");

    SmallVector<Value> operands;
    Block &operandBlock = aggregate.getBody().front();
    operands.reserve(operandBlock.getOperations().size());
    std::optional<uint64_t> expectedCount;
    std::optional<uint64_t> variantIndex;
    if (aggregateKind == rust::mir::RustAggregateKind::Adt && adtType &&
        adtType.getVariants().size() > 1) {
      variantIndex = static_cast<uint64_t>(variantIndexAttr.getInt());
      expectedCount = getVariantElementCount(adtType, *variantIndex);
    } else {
      expectedCount = getAggregateElementCount(dest->elementType);
    }
    if (!expectedCount)
      return assign.emitError("failed to infer aggregate element count");
    if (operandBlock.getOperations().size() != *expectedCount)
      return assign.emitError("aggregate operand count does not match "
                              "destination type");

    for (auto indexedOperand : llvm::enumerate(operandBlock)) {
      Operation *operandOp = &indexedOperand.value();
      Type expectedType =
          variantIndex
              ? getVariantElementType(adtType, *variantIndex,
                                      indexedOperand.index(), builder)
              : getAggregateElementType(dest->elementType,
                                        indexedOperand.index(), builder);
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
                       variantIndexAttr, aggregate.getDiscriminantAttr(),
                       assign.getSpanAttr())
                       .getResult();
    createStore(builder, assign.getLoc(), result, dest->slot);
    return success();
  }

  if (auto copyForDeref = dyn_cast<rust::mir::CopyForDerefOp>(rvalue)) {
    auto sourcePlace =
        dyn_cast_or_null<rust::mir::PlaceOp>(childAt(copyForDeref, 0));
    if (!sourcePlace)
      return assign.emitError("expected copy_for_deref source place");

    std::optional<Value> value =
        materializePlaceRead(sourcePlace, builder, assign.getLoc(), slots,
                             dest->elementType, assign.getSpanAttr());
    if (!value)
      return assign.emitError("failed to materialize copy_for_deref source");
    createStore(builder, assign.getLoc(), *value, dest->slot);
    return success();
  }

  if (auto repeat = dyn_cast<rust::mir::RepeatOp>(rvalue)) {
    auto arrayType = dyn_cast<rust::mir::TypedArrayType>(dest->elementType);
    if (!arrayType)
      return assign.emitError("repeat destination is not an array");

    int64_t signedCount = repeat.getCount();
    if (signedCount < 0)
      return assign.emitError("repeat count cannot be negative");
    uint64_t count = static_cast<uint64_t>(signedCount);
    if (count != arrayType.getLength())
      return assign.emitError("repeat count does not match destination array "
                              "length");

    Operation *operandOp = childAt(repeat, 0);
    if (!operandOp)
      return assign.emitError("expected repeat operand");

    SmallVector<Value> operands;
    operands.reserve(count);
    if (count > 0) {
      std::optional<Value> operand =
          materializeOperand(operandOp, builder, assign.getLoc(), slots,
                             arrayType.getElementType(), assign.getSpanAttr());
      if (!operand)
        return assign.emitError("failed to materialize repeat operand");
      for (uint64_t index = 0; index < count; ++index)
        operands.push_back(*operand);
    }

    Value result = mlir::rust::createOp<rust::mir::MakeAggregateOp>(
                       builder, assign.getLoc(), dest->elementType, operands,
                       IntegerAttr(), StringAttr(), assign.getSpanAttr())
                       .getResult();
    createStore(builder, assign.getLoc(), result, dest->slot);
    return success();
  }

  if (auto discriminant = dyn_cast<rust::mir::DiscriminantOp>(rvalue)) {
    auto sourcePlace =
        dyn_cast_or_null<rust::mir::PlaceOp>(childAt(discriminant, 0));
    if (!sourcePlace)
      return assign.emitError("expected discriminant source place");

    std::optional<Type> sourceType = inferPlaceType(sourcePlace, slots);
    if (!sourceType)
      return assign.emitError("failed to infer discriminant source type");

    std::optional<Value> aggregateValue =
        materializePlaceRead(sourcePlace, builder, assign.getLoc(), slots,
                             *sourceType, assign.getSpanAttr());
    if (!aggregateValue)
      return assign.emitError("failed to materialize discriminant source");

    Value value = mlir::rust::createOp<rust::mir::TypedDiscriminantOp>(
                      builder, assign.getLoc(), dest->elementType,
                      *aggregateValue, assign.getSpanAttr())
                      .getResult();
    createStore(builder, assign.getLoc(), value, dest->slot);
    return success();
  }

  if (auto ref = dyn_cast<rust::mir::RefOp>(rvalue)) {
    auto sourcePlace = dyn_cast_or_null<rust::mir::PlaceOp>(childAt(ref, 0));
    if (!sourcePlace)
      return assign.emitError("expected ref source place");

    std::optional<PlaceAddress> address = materializePlaceAddress(
        sourcePlace, builder, assign.getLoc(), slots, assign.getSpanAttr());
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
    createStore(builder, assign.getLoc(), result, dest->slot);
    return success();
  }

  if (auto addressOf = dyn_cast<rust::mir::AddressOfOp>(rvalue)) {
    auto sourcePlace =
        dyn_cast_or_null<rust::mir::PlaceOp>(childAt(addressOf, 0));
    if (!sourcePlace)
      return assign.emitError("expected address_of source place");

    std::optional<PlaceAddress> address = materializePlaceAddress(
        sourcePlace, builder, assign.getLoc(), slots, assign.getSpanAttr());
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
    createStore(builder, assign.getLoc(), result, dest->slot);
    return success();
  }

  if (auto len = dyn_cast<rust::mir::LenOp>(rvalue)) {
    auto sourcePlace = dyn_cast_or_null<rust::mir::PlaceOp>(childAt(len, 0));
    if (!sourcePlace)
      return assign.emitError("expected len source place");

    std::optional<Type> sourceType = inferPlaceType(sourcePlace, slots);
    rust::mir::TypedArrayType arrayType;
    if (sourceType)
      arrayType = dyn_cast<rust::mir::TypedArrayType>(*sourceType);
    if (arrayType) {
      Attribute valueAttr = builder.getI64IntegerAttr(
          static_cast<int64_t>(arrayType.getLength()));
      StringAttr debugAttr =
          builder.getStringAttr(std::to_string(arrayType.getLength()));
      Value result = mlir::rust::createOp<rust::mir::TypedConstOp>(
                         builder, assign.getLoc(), dest->elementType, valueAttr,
                         debugAttr, assign.getSpanAttr())
                         .getResult();
      createStore(builder, assign.getLoc(), result, dest->slot);
      return success();
    }

    if (sourceType && isa<rust::mir::TypedSliceType>(*sourceType)) {
      std::optional<PlaceAddress> address = materializePlaceAddress(
          sourcePlace, builder, assign.getLoc(), slots, assign.getSpanAttr());
      if (!address)
        return assign.emitError("failed to materialize len source address");
      Value result = mlir::rust::createOp<rust::mir::PtrMetadataOp>(
                         builder, assign.getLoc(), dest->elementType,
                         address->slot, assign.getSpanAttr())
                         .getMetadata();
      createStore(builder, assign.getLoc(), result, dest->slot);
      return success();
    }

    return assign.emitError("only array and slice len rvalues can be lifted");
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
    createStore(builder, assign.getLoc(), *value, dest->slot);
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
  auto destination = dyn_cast_or_null<rust::mir::PlaceOp>(childAt(op, 1));
  if (!destination)
    return op.emitError("expected call destination place");
  std::optional<uint64_t> target = op.getTarget();

  if (rustName && hasUnreachableUnwind(op)) {
    StringRef rustNameRef(*rustName);
    if (target && isPanicArgumentsFromStrCallName(rustNameRef)) {
      mlir::rust::createOp<rust::mir::TypedGotoOp>(
          builder, op.getLoc(),
          builder.getI64IntegerAttr(static_cast<int64_t>(*target)),
          op.getSpanAttr());
      return success();
    }
    if (!target && isPanicFmtCallName(rustNameRef)) {
      rust::mir::RustAbiAttr abiAttr =
          rust::mir::RustAbiAttr::get(op.getContext(), rust::mir::RustAbi::C);
      mlir::rust::createOp<rust::mir::TypedCallOp>(
          builder, op.getLoc(), TypeRange(),
          FlatSymbolRefAttr::get(op.getContext(), "__rtl_abort"), ValueRange(),
          builder.getStringAttr("__rtl_abort"), abiAttr, StringAttr(),
          StringAttr(), StringAttr(), StringAttr(), StringAttr(), StringAttr(),
          BoolAttr(), IntegerAttr(), op.getUnwindAttr(), op.getSpanAttr());
      mlir::rust::createOp<rust::mir::TypedUnreachableOp>(
          builder, op.getLoc(), op.getSpanAttr());
      return success();
    }
  }

  if (rustName && target) {
    StringRef rustNameRef(*rustName);
    if (isRangeInclusiveNewCallName(rustNameRef))
      return lowerRangeInclusiveNewCall(op, builder, slots, destination,
                                        *target);
    if (isRangeIntoIteratorCall(op, rustNameRef))
      return lowerRangeIntoIteratorCall(op, builder, slots, destination,
                                        *target);
    if (std::optional<RangeIndexKind> rangeKind =
            getRangeIndexKind(op, rustNameRef))
      return lowerRangeIndexCall(op, builder, slots, destination, *target,
                                 *rangeKind);
  }

  Location loc = op.getLoc();
  StringAttr spanAttr = op.getSpanAttr();
  bool closureTraitCall = isClosureTraitCall(op);
  SmallVector<Value> args;
  Block &callBody = op.getBody().front();
  for (auto indexedChild : llvm::enumerate(callBody)) {
    if (indexedChild.index() < 2)
      continue;
    Operation *argOp = &indexedChild.value();
    Type expectedType = inferOperandType(argOp, slots).value_or(Type());
    if (closureTraitCall && indexedChild.index() == 3 && expectedType &&
        isa<rust::mir::UnitType>(expectedType))
      continue;
    std::optional<Value> arg =
        materializeOperand(argOp, builder, loc, slots, expectedType, spanAttr);
    if (!arg)
      return op.emitError("failed to materialize call argument");
    args.push_back(*arg);
  }

  if (closureTraitCall) {
    if (args.empty())
      return op.emitError("closure trait call is missing receiver argument");
    if (args.size() > 2)
      return op.emitError("closure trait call has unexpected argument shape");

    SmallVector<Value> callArgs;
    callArgs.push_back(args.front());
    if (args.size() == 2) {
      auto tupleType = dyn_cast<rust::mir::TypedTupleType>(args[1].getType());
      if (!tupleType)
        return op.emitError("closure RustCall argument is not a tuple");
      for (auto indexedType : llvm::enumerate(tupleType.getElementTypes())) {
        IntegerAttr indexAttr = builder.getI64IntegerAttr(
            static_cast<int64_t>(indexedType.index()));
        Value field = mlir::rust::createOp<rust::mir::FieldOp>(
                          builder, loc, indexedType.value(), args[1],
                          indexAttr, IntegerAttr(), spanAttr)
                          .getResult();
        callArgs.push_back(field);
      }
    }
    args = std::move(callArgs);
  }

  SmallVector<Type> resultTypes;
  std::optional<Type> destinationType = inferPlaceType(destination, slots);
  if (!destinationType)
    return op.emitError("failed to infer call destination type");
  if (!target && !isa<rust::mir::NeverType>(*destinationType))
    return op.emitError(
        "cannot lift call without return target unless destination is never");
  StringAttr bridgeSymbol = op.getCalleeBridgeSymbolAttr();
  bool bridgeUsesOutPointer = bridgeSymbol && isOptionAdtType(*destinationType);
  if (!isa<rust::mir::UnitType, rust::mir::NeverType>(*destinationType) &&
      !bridgeUsesOutPointer)
    resultTypes.push_back(*destinationType);

  if (!rustName) {
    Operation *calleeOp = childAt(op, 0);
    if (!calleeOp)
      return op.emitError("expected indirect call callee");

    SmallVector<Type> argTypes;
    argTypes.reserve(args.size());
    for (Value arg : args)
      argTypes.push_back(arg.getType());
    FunctionType expectedCalleeType =
        FunctionType::get(op.getContext(), argTypes, resultTypes);
    std::optional<Value> callee = materializeOperand(
        calleeOp, builder, loc, slots, expectedCalleeType, spanAttr);
    if (!callee)
      return op.emitError("failed to materialize indirect call callee");

    auto calleeType = dyn_cast<FunctionType>((*callee).getType());
    if (!calleeType)
      return op.emitError(
          "expected indirect call callee to have function type");
    if (calleeType != expectedCalleeType)
      return op.emitError("indirect call callee type mismatch, expected ")
             << expectedCalleeType << ", got " << calleeType;

    auto typedCall = mlir::rust::createOp<rust::mir::TypedCallIndirectOp>(
        builder, loc, resultTypes, *callee, args, op.getTargetAttr(),
        op.getUnwindAttr(), spanAttr);
    if (!resultTypes.empty()) {
      std::optional<PlaceAddress> dest =
          materializePlaceAddress(destination, builder, loc, slots, spanAttr);
      if (!dest)
        return op.emitError("failed to materialize call destination");
      createStore(builder, loc, typedCall->getResult(0), dest->slot);
    }

    if (target) {
      mlir::rust::createOp<rust::mir::TypedGotoOp>(
          builder, loc,
          builder.getI64IntegerAttr(static_cast<int64_t>(*target)), spanAttr);
    } else {
      mlir::rust::createOp<rust::mir::TypedUnreachableOp>(builder, loc,
                                                          spanAttr);
    }
    return success();
  }

  StringRef rustNameRef(*rustName);
  if (bridgeUsesOutPointer) {
    std::optional<PlaceAddress> dest =
        materializePlaceAddress(destination, builder, loc, slots, spanAttr);
    if (!dest)
      return op.emitError("failed to materialize bridge output destination");
    args.push_back(dest->slot);
  }
  bool isCAbi = bridgeSymbol || isCAbiCall(op);
  rust::mir::RustAbiAttr abiAttr = rust::mir::RustAbiAttr::get(
      op.getContext(),
      isCAbi ? rust::mir::RustAbi::C : rust::mir::RustAbi::Rust);
  std::string callee = bridgeSymbol ? bridgeSymbol.getValue().str()
                       : isCAbi     ? getCAbiSymbol(rustNameRef)
                                    : getTypedSymbol(rustNameRef);
  auto typedCall = mlir::rust::createOp<rust::mir::TypedCallOp>(
      builder, loc, resultTypes,
      FlatSymbolRefAttr::get(op.getContext(), callee), args,
      builder.getStringAttr(*rustName), abiAttr, op.getCalleeDefAttr(),
      op.getCalleeTypeAttr(), op.getCalleeGenericArgsAttr(),
      op.getCalleeInputsAttr(), op.getCalleeOutputAttr(),
      op.getCalleeBridgeSymbolAttr(), op.getCalleeCVariadicAttr(),
      op.getTargetAttr(), op.getUnwindAttr(), spanAttr);

  if (!resultTypes.empty()) {
    std::optional<PlaceAddress> dest =
        materializePlaceAddress(destination, builder, loc, slots, spanAttr);
    if (!dest)
      return op.emitError("failed to materialize call destination");
    createStore(builder, loc, typedCall->getResult(0), dest->slot);
  }

  if (target) {
    mlir::rust::createOp<rust::mir::TypedGotoOp>(
        builder, loc, builder.getI64IntegerAttr(static_cast<int64_t>(*target)),
        spanAttr);
  } else {
    mlir::rust::createOp<rust::mir::TypedUnreachableOp>(builder, loc,
                                                        spanAttr);
  }
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
      op.getUnwindAttr(), spanAttr);
  mlir::rust::createOp<rust::mir::TypedGotoOp>(
      builder, op.getLoc(),
      builder.getI64IntegerAttr(static_cast<int64_t>(op.getTarget())),
      spanAttr);
  return success();
}

rust::mir::TypedBlockOp createSyntheticTypedBlock(DropLoweringContext &ctx,
                                                  Location loc,
                                                  StringAttr spanAttr) {
  OpBuilder::InsertionGuard guard(ctx.builder);
  ctx.builder.setInsertionPointToEnd(ctx.typedFuncBlock);
  auto block = mlir::rust::createOp<rust::mir::TypedBlockOp>(
      ctx.builder, loc, ctx.builder.getI64IntegerAttr(ctx.nextSyntheticBlock++),
      spanAttr);
  block.getBody().push_back(new Block());
  return block;
}

bool mayNeedDropGlue(Type type, ModuleOp module) {
  if (isTriviallyDroppableType(type))
    return false;

  if (auto tupleType = dyn_cast<rust::mir::TypedTupleType>(type))
    return llvm::any_of(tupleType.getElementTypes(),
                        [&](Type elementType) {
                          return mayNeedDropGlue(elementType, module);
                        });

  if (auto arrayType = dyn_cast<rust::mir::TypedArrayType>(type))
    return mayNeedDropGlue(arrayType.getElementType(), module);

  if (auto adtType = dyn_cast<rust::mir::AdtType>(type)) {
    if (getLocalDropImpl(adtType, module))
      return true;

    if (!adtType.isInitialized())
      return true;

    for (Type variant : adtType.getVariants()) {
      if (isa<rust::mir::UnitType>(variant))
        continue;
      auto tupleType = dyn_cast<rust::mir::TypedTupleType>(variant);
      if (!tupleType)
        return true;
      if (llvm::any_of(tupleType.getElementTypes(), [&](Type fieldType) {
            return mayNeedDropGlue(fieldType, module);
          }))
        return true;
    }
    return false;
  }

  return true;
}

LogicalResult emitUnsupportedDropGlue(Type type, DropLoweringContext &ctx) {
  ctx.diagnosticOp
      ->emitError("cannot lift drop for type requiring unsupported drop glue: ")
      << type;
  return failure();
}

DropAddress makePlaceDropAddress(rust::mir::PlaceOp place, Type elementType) {
  return DropAddress{
      elementType,
      [place](DropLoweringContext &ctx, Location loc,
              StringAttr spanAttr) -> std::optional<Value> {
        std::optional<PlaceAddress> address =
            materializePlaceAddress(place, ctx.builder, loc, ctx.slots,
                                    spanAttr);
        if (!address)
          return std::nullopt;
        return address->slot;
      }};
}

std::optional<DropAddress> makeFieldDropAddress(DropAddress base,
                                                Type aggregateType,
                                                uint64_t fieldIndex,
                                                IntegerAttr variantIndex,
                                                DropLoweringContext &ctx) {
  if (fieldIndex > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
    return std::nullopt;

  IntegerAttr fieldIndexAttr =
      ctx.builder.getI64IntegerAttr(static_cast<int64_t>(fieldIndex));
  Type fieldType = getIndexedElementType(aggregateType, fieldIndexAttr,
                                         variantIndex);
  if (!fieldType)
    return std::nullopt;

  MLIRContext *context = aggregateType.getContext();
  return DropAddress{
      fieldType,
      [base, fieldType, fieldIndexAttr, variantIndex, context](
          DropLoweringContext &ctx, Location loc,
          StringAttr spanAttr) -> std::optional<Value> {
        std::optional<Value> baseAddress =
            base.materialize(ctx, loc, spanAttr);
        if (!baseAddress)
          return std::nullopt;

        auto addressType = rust::mir::TypedAddrType::get(context, fieldType);
        return mlir::rust::createOp<rust::mir::FieldAddrOp>(
                   ctx.builder, loc, addressType, *baseAddress, fieldIndexAttr,
                   variantIndex, spanAttr)
            .getAddress();
      }};
}

std::optional<DropAddress> makeArrayElementDropAddress(DropAddress base,
                                                       uint64_t elementIndex,
                                                       DropLoweringContext &ctx) {
  if (elementIndex >
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
    return std::nullopt;

  auto arrayType = dyn_cast<rust::mir::TypedArrayType>(base.elementType);
  if (!arrayType)
    return std::nullopt;

  Type elementType = arrayType.getElementType();
  MLIRContext *context = arrayType.getContext();
  int64_t signedIndex = static_cast<int64_t>(elementIndex);
  return DropAddress{
      elementType,
      [base, elementType, context, signedIndex](
          DropLoweringContext &ctx, Location loc,
          StringAttr spanAttr) -> std::optional<Value> {
        std::optional<Value> baseAddress =
            base.materialize(ctx, loc, spanAttr);
        if (!baseAddress)
          return std::nullopt;

        Type indexType = rust::mir::IntType::getFromSpelling(context, "usize");
        Value index =
            createTypedIntegerConst(ctx.builder, loc, indexType, signedIndex,
                                    spanAttr);
        auto addressType = rust::mir::TypedAddrType::get(context, elementType);
        return mlir::rust::createOp<rust::mir::IndexAddrOp>(
                   ctx.builder, loc, addressType, *baseAddress, index,
                   spanAttr)
            .getAddress();
      }};
}

void emitDropBridgeCall(Value address, StringAttr bridgeSymbol,
                        DropLoweringContext &ctx, Location loc,
                        StringAttr spanAttr, DropCallAttrs callAttrs) {
  MLIRContext *context = bridgeSymbol.getContext();
  mlir::rust::createOp<rust::mir::TypedCallOp>(
      ctx.builder, loc, TypeRange(),
      FlatSymbolRefAttr::get(context, bridgeSymbol.getValue()),
      ValueRange{address}, ctx.builder.getStringAttr("core::ptr::drop_in_place"),
      rust::mir::RustAbiAttr::get(context, rust::mir::RustAbi::C),
      StringAttr(), StringAttr(), StringAttr(), StringAttr(), StringAttr(),
      bridgeSymbol, BoolAttr(), callAttrs.targetAttr, callAttrs.unwindAttr,
      spanAttr);
}

void emitLocalDropImplCall(Type type, Value address, const LocalDropImpl &dropImpl,
                           DropLoweringContext &ctx, Location loc,
                           StringAttr spanAttr, DropCallAttrs callAttrs) {
  MLIRContext *context = type.getContext();
  Type selfType = rust::mir::TypedRefType::get(
      context, rust::mir::RustMutability::Mut, type);
  Value selfRef =
      mlir::rust::createOp<rust::mir::BorrowOp>(
          ctx.builder, loc, selfType, address,
          rust::mir::RustBorrowKindAttr::get(
              context, rust::mir::RustBorrowKind::Mut),
          rust::mir::RustMutabilityAttr::get(
              context, rust::mir::RustMutability::Mut),
          StringAttr(), spanAttr)
          .getResult();

  mlir::rust::createOp<rust::mir::TypedCallOp>(
      ctx.builder, loc, TypeRange(),
      FlatSymbolRefAttr::get(context, dropImpl.typedSymbol), ValueRange{selfRef},
      ctx.builder.getStringAttr(dropImpl.rustName),
      rust::mir::RustAbiAttr::get(context, rust::mir::RustAbi::Rust),
      StringAttr(), StringAttr(), StringAttr(), StringAttr(), StringAttr(),
      StringAttr(), BoolAttr(), callAttrs.targetAttr, callAttrs.unwindAttr,
      spanAttr);
}

LogicalResult emitDropGlueTo(Type type, DropAddress address,
                             DropLoweringContext &ctx, Location loc,
                             StringAttr spanAttr, int64_t target,
                             DropCallAttrs callAttrs);

LogicalResult emitDropSequenceTo(ArrayRef<DropAddress> fields,
                                 DropLoweringContext &ctx, Location loc,
                                 StringAttr spanAttr, int64_t target,
                                 DropCallAttrs callAttrs) {
  SmallVector<DropAddress> nontrivialFields;
  for (const DropAddress &field : fields)
    if (mayNeedDropGlue(field.elementType, ctx.module))
      nontrivialFields.push_back(field);

  if (nontrivialFields.empty()) {
    createTypedGoto(ctx.builder, loc, target, spanAttr);
    return success();
  }

  for (auto indexedField : llvm::enumerate(nontrivialFields)) {
    int64_t continuationTarget = target;
    rust::mir::TypedBlockOp continuationBlock;
    if (indexedField.index() + 1 != nontrivialFields.size()) {
      continuationBlock = createSyntheticTypedBlock(ctx, loc, spanAttr);
      continuationTarget =
          static_cast<int64_t>(continuationBlock.getIndex());
    }

    if (failed(emitDropGlueTo(indexedField.value().elementType,
                              indexedField.value(), ctx, loc, spanAttr,
                              continuationTarget, callAttrs)))
      return failure();

    if (continuationBlock)
      ctx.builder.setInsertionPointToEnd(
          &continuationBlock.getBody().front());
  }

  return success();
}

LogicalResult appendTupleFieldDropAddresses(
    Type aggregateType, rust::mir::TypedTupleType tupleType, DropAddress address,
    IntegerAttr variantIndex, DropLoweringContext &ctx,
    SmallVectorImpl<DropAddress> &fields) {
  for (uint64_t fieldIndex = 0; fieldIndex < tupleType.getElementTypes().size();
       ++fieldIndex) {
    std::optional<DropAddress> fieldAddress = makeFieldDropAddress(
        address, aggregateType, fieldIndex, variantIndex, ctx);
    if (!fieldAddress)
      return emitUnsupportedDropGlue(aggregateType, ctx);
    fields.push_back(*fieldAddress);
  }
  return success();
}

LogicalResult emitTupleDropGlueTo(rust::mir::TypedTupleType tupleType,
                                  DropAddress address,
                                  DropLoweringContext &ctx, Location loc,
                                  StringAttr spanAttr, int64_t target,
                                  DropCallAttrs callAttrs) {
  SmallVector<DropAddress> fields;
  if (failed(appendTupleFieldDropAddresses(tupleType, tupleType, address,
                                           IntegerAttr(), ctx, fields)))
    return failure();
  return emitDropSequenceTo(fields, ctx, loc, spanAttr, target, callAttrs);
}

LogicalResult emitArrayDropGlueTo(rust::mir::TypedArrayType arrayType,
                                  DropAddress address,
                                  DropLoweringContext &ctx, Location loc,
                                  StringAttr spanAttr, int64_t target,
                                  DropCallAttrs callAttrs) {
  SmallVector<DropAddress> elements;
  for (uint64_t elementIndex = 0; elementIndex < arrayType.getLength();
       ++elementIndex) {
    std::optional<DropAddress> elementAddress =
        makeArrayElementDropAddress(address, elementIndex, ctx);
    if (!elementAddress)
      return emitUnsupportedDropGlue(arrayType, ctx);
    elements.push_back(*elementAddress);
  }
  return emitDropSequenceTo(elements, ctx, loc, spanAttr, target, callAttrs);
}

LogicalResult emitSingleVariantAdtDropGlueTo(rust::mir::AdtType adtType,
                                             DropAddress address,
                                             DropLoweringContext &ctx,
                                             Location loc, StringAttr spanAttr,
                                             int64_t target,
                                             DropCallAttrs callAttrs) {
  ArrayRef<Type> variants = adtType.getVariants();
  if (variants.empty())
    return emitUnsupportedDropGlue(adtType, ctx);

  if (isa<rust::mir::UnitType>(variants.front())) {
    createTypedGoto(ctx.builder, loc, target, spanAttr);
    return success();
  }

  auto tupleType = dyn_cast<rust::mir::TypedTupleType>(variants.front());
  if (!tupleType)
    return emitUnsupportedDropGlue(adtType, ctx);

  SmallVector<DropAddress> fields;
  if (failed(appendTupleFieldDropAddresses(adtType, tupleType, address,
                                           IntegerAttr(), ctx, fields)))
    return failure();
  return emitDropSequenceTo(fields, ctx, loc, spanAttr, target, callAttrs);
}

LogicalResult emitEnumVariantFieldsTo(rust::mir::AdtType adtType,
                                      uint64_t variantIndex,
                                      DropAddress address,
                                      DropLoweringContext &ctx, Location loc,
                                      StringAttr spanAttr, int64_t target,
                                      DropCallAttrs callAttrs) {
  ArrayRef<Type> variants = adtType.getVariants();
  if (variantIndex >= variants.size())
    return emitUnsupportedDropGlue(adtType, ctx);

  if (isa<rust::mir::UnitType>(variants[variantIndex])) {
    createTypedGoto(ctx.builder, loc, target, spanAttr);
    return success();
  }

  auto tupleType = dyn_cast<rust::mir::TypedTupleType>(variants[variantIndex]);
  if (!tupleType)
    return emitUnsupportedDropGlue(adtType, ctx);

  SmallVector<DropAddress> fields;
  IntegerAttr variantIndexAttr =
      ctx.builder.getI64IntegerAttr(static_cast<int64_t>(variantIndex));
  if (failed(appendTupleFieldDropAddresses(adtType, tupleType, address,
                                           variantIndexAttr, ctx, fields)))
    return failure();
  return emitDropSequenceTo(fields, ctx, loc, spanAttr, target, callAttrs);
}

LogicalResult emitMultiVariantAdtDropGlueTo(rust::mir::AdtType adtType,
                                            DropAddress address,
                                            DropLoweringContext &ctx,
                                            Location loc, StringAttr spanAttr,
                                            int64_t target,
                                            DropCallAttrs callAttrs) {
  SmallVector<std::pair<uint64_t, rust::mir::TypedBlockOp>> variantBlocks;
  for (auto indexedVariant : llvm::enumerate(adtType.getVariants())) {
    Type variant = indexedVariant.value();
    if (isa<rust::mir::UnitType>(variant))
      continue;

    auto tupleType = dyn_cast<rust::mir::TypedTupleType>(variant);
    if (!tupleType)
      return emitUnsupportedDropGlue(adtType, ctx);

    bool variantNeedsDrop =
        llvm::any_of(tupleType.getElementTypes(), [&](Type fieldType) {
          return mayNeedDropGlue(fieldType, ctx.module);
        });
    if (!variantNeedsDrop)
      continue;

    variantBlocks.push_back({indexedVariant.index(),
                             createSyntheticTypedBlock(ctx, loc, spanAttr)});
  }

  if (variantBlocks.empty()) {
    createTypedGoto(ctx.builder, loc, target, spanAttr);
    return success();
  }

  std::optional<Value> aggregateAddress =
      address.materialize(ctx, loc, spanAttr);
  if (!aggregateAddress)
    return ctx.diagnosticOp->emitError(
        "failed to materialize drop place address");

  Value aggregate = createLoad(ctx.builder, loc, *aggregateAddress, adtType);
  Type discriminantType =
      rust::mir::IntType::getFromSpelling(adtType.getContext(), "isize");
  Value discriminant =
      mlir::rust::createOp<rust::mir::TypedDiscriminantOp>(
          ctx.builder, loc, discriminantType, aggregate, spanAttr)
          .getResult();

  SmallVector<NamedAttribute> targets;
  targets.push_back(ctx.builder.getNamedAttr(
      "default", ctx.builder.getI64IntegerAttr(target)));
  for (auto [variantIndex, variantBlock] : variantBlocks) {
    std::string caseName =
        ("case_" + llvm::Twine(static_cast<uint64_t>(variantIndex))).str();
    targets.push_back(ctx.builder.getNamedAttr(
        caseName, ctx.builder.getI64IntegerAttr(
                      static_cast<int64_t>(variantBlock.getIndex()))));
  }

  mlir::rust::createOp<rust::mir::TypedSwitchIntOp>(
      ctx.builder, loc, discriminant, ctx.builder.getDictionaryAttr(targets),
      spanAttr);

  for (auto [variantIndex, variantBlock] : variantBlocks) {
    ctx.builder.setInsertionPointToEnd(&variantBlock.getBody().front());
    if (failed(emitEnumVariantFieldsTo(adtType, variantIndex, address, ctx, loc,
                                       spanAttr, target, callAttrs)))
      return failure();
  }

  return success();
}

LogicalResult emitAdtStructuralDropGlueTo(rust::mir::AdtType adtType,
                                          DropAddress address,
                                          DropLoweringContext &ctx,
                                          Location loc, StringAttr spanAttr,
                                          int64_t target,
                                          DropCallAttrs callAttrs) {
  if (!adtType.isInitialized() || adtType.getVariants().empty())
    return emitUnsupportedDropGlue(adtType, ctx);

  if (adtType.getVariants().size() == 1)
    return emitSingleVariantAdtDropGlueTo(adtType, address, ctx, loc, spanAttr,
                                          target, callAttrs);

  return emitMultiVariantAdtDropGlueTo(adtType, address, ctx, loc, spanAttr,
                                       target, callAttrs);
}

LogicalResult emitDropGlueTo(Type type, DropAddress address,
                             DropLoweringContext &ctx, Location loc,
                             StringAttr spanAttr, int64_t target,
                             DropCallAttrs callAttrs) {
  if (!mayNeedDropGlue(type, ctx.module)) {
    createTypedGoto(ctx.builder, loc, target, spanAttr);
    return success();
  }

  if (std::optional<LocalDropImpl> dropImpl =
          getLocalDropImpl(type, ctx.module)) {
    std::optional<Value> dropAddress = address.materialize(ctx, loc, spanAttr);
    if (!dropAddress)
      return ctx.diagnosticOp->emitError(
          "failed to materialize drop place address");
    emitLocalDropImplCall(type, *dropAddress, *dropImpl, ctx, loc, spanAttr,
                          callAttrs);
  }

  if (auto tupleType = dyn_cast<rust::mir::TypedTupleType>(type))
    return emitTupleDropGlueTo(tupleType, address, ctx, loc, spanAttr, target,
                               callAttrs);

  if (auto arrayType = dyn_cast<rust::mir::TypedArrayType>(type))
    return emitArrayDropGlueTo(arrayType, address, ctx, loc, spanAttr, target,
                               callAttrs);

  if (auto adtType = dyn_cast<rust::mir::AdtType>(type))
    return emitAdtStructuralDropGlueTo(adtType, address, ctx, loc, spanAttr,
                                       target, callAttrs);

  return emitUnsupportedDropGlue(type, ctx);
}

LogicalResult lowerDrop(rust::mir::DropOp op, DropLoweringContext &ctx) {
  auto place = dyn_cast_or_null<rust::mir::PlaceOp>(childAt(op, 0));
  if (!place)
    return op.emitError("expected drop place");

  std::optional<Type> placeType = inferPlaceType(place, ctx.slots);
  if (!placeType)
    return op.emitError("failed to infer drop place type");

  Location loc = op.getLoc();
  StringAttr spanAttr = op.getSpanAttr();
  int64_t target = static_cast<int64_t>(op.getTarget());
  DropCallAttrs callAttrs{op.getTargetAttr(), op.getUnwindAttr()};
  DropAddress address = makePlaceDropAddress(place, *placeType);

  if (StringAttr bridgeSymbol = op.getCalleeBridgeSymbolAttr()) {
    std::optional<Value> dropAddress = address.materialize(ctx, loc, spanAttr);
    if (!dropAddress)
      return op.emitError("failed to materialize drop place address");

    emitDropBridgeCall(*dropAddress, bridgeSymbol, ctx, loc, spanAttr,
                       callAttrs);
    createTypedGoto(ctx.builder, loc, target, spanAttr);
    return success();
  }

  return emitDropGlueTo(*placeType, address, ctx, loc, spanAttr, target,
                        callAttrs);
}

LogicalResult lowerSetDiscriminant(rust::mir::SetDiscriminantOp op,
                                   OpBuilder &builder,
                                   llvm::StringMap<LocalSlot> &slots) {
  auto place = dyn_cast_or_null<rust::mir::PlaceOp>(childAt(op, 0));
  if (!place)
    return op.emitError("expected set_discriminant place");

  std::optional<Type> placeType = inferPlaceType(place, slots);
  if (!placeType)
    return op.emitError("failed to infer set_discriminant place type");

  auto adtType = dyn_cast<rust::mir::AdtType>(*placeType);
  if (!adtType)
    return op.emitError("set_discriminant place must be an enum ADT");
  if (!adtType.isInitialized() || adtType.getVariants().empty())
    return op.emitError("set_discriminant place has uninitialized ADT type");

  int64_t variantIndex = op.getVariantIndex();
  if (variantIndex < 0 ||
      static_cast<size_t>(variantIndex) >= adtType.getVariants().size())
    return op.emitError("set_discriminant variant index out of range");
  if (adtType.getVariants().size() <= 1)
    return success();

  std::optional<PlaceAddress> address =
      materializePlaceAddress(place, builder, op.getLoc(), slots,
                              op.getSpanAttr());
  if (!address)
    return op.emitError("failed to materialize set_discriminant place address");

  mlir::rust::createOp<rust::mir::TypedSetDiscriminantOp>(
      builder, op.getLoc(), address->slot, op.getVariantIndexAttr(),
      op.getDiscriminantAttr(), op.getSpanAttr());
  return success();
}

bool isNoOpStatement(Operation *op) {
  return isa<rust::mir::FakeReadOp, rust::mir::DeinitOp,
             rust::mir::StorageLiveOp,
             rust::mir::StorageDeadOp, rust::mir::RetagOp,
             rust::mir::PlaceMentionOp, rust::mir::AscribeUserTypeOp,
             rust::mir::CoverageOp, rust::mir::ConstEvalCounterOp,
             rust::mir::NopOp>(op);
}

bool isKnownNonNoOpStatement(Operation *op) {
  return isa<rust::mir::IntrinsicOp>(op);
}

struct LiftTypedMIRPass
    : public mlir::impl::LiftTypedMIRPassBase<LiftTypedMIRPass> {
  using Base::Base;

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

      std::string typedName = getTypedSymbol(symName.getValue());
      auto typedFunc = mlir::rust::createOp<rust::mir::TypedFuncOp>(
          builder, func.getLoc(), typedName, func.getRustNameAttr(),
          func.getSignatureAttr(), func.getArgCountAttr(), func.getSpanAttr());
      Region &typedBody = typedFunc.getBody();
      typedBody.push_back(new Block());
      builder.setInsertionPointToEnd(&typedBody.front());

      llvm::StringSet<> addressTakenLocals = collectAddressTakenLocals(func);
      llvm::StringSet<> panicAbortFormattingLocals =
          collectPanicAbortFormattingLocals(func);
      llvm::StringMap<LocalSlot> slots;
      Region &mirBody = func.getBody();
      int64_t nextSyntheticBlock = 0;
      for (Operation &child : mirBody.front()) {
        auto blockOp = dyn_cast<rust::mir::BlockOp>(child);
        if (!blockOp)
          continue;
        IntegerAttr index = blockOp.getIndexAttr();
        if (!index)
          continue;
        int64_t blockIndex = static_cast<int64_t>(index.getInt());
        if (blockIndex >= nextSyntheticBlock)
          nextSyntheticBlock = blockIndex + 1;
      }

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
          if (panicAbortFormattingLocals.contains(nameAttr.getValue()))
            continue;

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
          } else if (auto unreachable =
                         dyn_cast<rust::mir::UnreachableOp>(mirOp)) {
            mlir::rust::createOp<rust::mir::TypedUnreachableOp>(
                builder, unreachable.getLoc(), unreachable.getSpanAttr());
            hasTypedTerminator = true;
          } else if (auto resume = dyn_cast<rust::mir::ResumeOp>(mirOp)) {
            mlir::rust::createOp<rust::mir::TypedUnreachableOp>(
                builder, resume.getLoc(), resume.getSpanAttr());
            hasTypedTerminator = true;
          } else if (auto abort = dyn_cast<rust::mir::AbortOp>(mirOp)) {
            mlir::rust::createOp<rust::mir::TypedUnreachableOp>(
                builder, abort.getLoc(), abort.getSpanAttr());
            hasTypedTerminator = true;
          } else if (auto call = dyn_cast<rust::mir::CallOp>(mirOp)) {
            if (failed(lowerCall(call, builder, slots)))
              sawFailure = true;
            hasTypedTerminator = true;
          } else if (auto setDiscriminant =
                         dyn_cast<rust::mir::SetDiscriminantOp>(mirOp)) {
            if (failed(lowerSetDiscriminant(setDiscriminant, builder, slots)))
              sawFailure = true;
          } else if (isNoOpStatement(&mirOp)) {
            continue;
          } else if (isKnownNonNoOpStatement(&mirOp)) {
            mirOp.emitError("cannot lift non-no-op MIR statement yet");
            sawFailure = true;
          } else if (isa<rust::mir::UnsupportedStatementOp,
                         rust::mir::UnsupportedTerminatorOp>(mirOp)) {
            mirOp.emitError("cannot lift unsupported MIR operation");
            sawFailure = true;
          } else if (auto drop = dyn_cast<rust::mir::DropOp>(mirOp)) {
            DropLoweringContext dropContext{module, builder, slots,
                                            &typedBody.front(),
                                            nextSyntheticBlock,
                                            drop.getOperation()};
            if (failed(lowerDrop(drop, dropContext)))
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

    if (sawFailure) {
      signalPassFailure();
      return;
    }

    if (eraseSourceMIR)
      for (rust::mir::FuncOp func : llvm::reverse(funcs))
        func.erase();
  }
};
} // namespace

std::unique_ptr<Pass> mlir::rust::createLiftTypedMIRPass() {
  return std::make_unique<LiftTypedMIRPass>();
}

std::unique_ptr<Pass>
mlir::rust::createLiftTypedMIRPass(LiftTypedMIRPassOptions options) {
  return std::make_unique<LiftTypedMIRPass>(std::move(options));
}

void mlir::rust::registerRustMIRPasses() { mlir::registerLiftTypedMIRPass(); }
