//===- RustTypes.cpp - Rust MIR dialect types -------------------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/RustMIR/IR/RustTypes.h"

#include "mlir/Dialect/RustMIR/IR/RustMIRDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/Types.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/TypeSwitch.h"

using namespace mlir;
using namespace mlir::rust::mir;

namespace mlir::rust::mir::detail {
/// Mutable storage backing the identified, recursion-capable AdtType. Uniqued
/// on `name` only; `variants` is the mutable body set once via `mutate`.
struct AdtTypeStorage : public TypeStorage {
  using KeyTy = StringRef;

  explicit AdtTypeStorage(StringRef name) : name(name) {}

  bool operator==(const KeyTy &key) const { return key == name; }

  static llvm::hash_code hashKey(const KeyTy &key) {
    return llvm::hash_value(key);
  }

  static AdtTypeStorage *construct(TypeStorageAllocator &allocator,
                                   const KeyTy &key) {
    return new (allocator.allocate<AdtTypeStorage>())
        AdtTypeStorage(allocator.copyInto(key));
  }

  LogicalResult mutate(TypeStorageAllocator &allocator,
                       ArrayRef<Type> newVariants) {
    if (initialized)
      return success(ArrayRef<Type>(variants) == newVariants);
    variants = allocator.copyInto(newVariants);
    initialized = true;
    return success();
  }

  StringRef name;
  ArrayRef<Type> variants;
  bool initialized = false;
};
} // namespace mlir::rust::mir::detail

#define GET_TYPEDEF_CLASSES
#include "mlir/Dialect/RustMIR/IR/RustOpsTypes.cpp.inc"

StringRef AdtType::getName() const { return getImpl()->name; }

ArrayRef<Type> AdtType::getVariants() const { return getImpl()->variants; }

AdtType AdtType::getIdentified(MLIRContext *context, StringRef name) {
  return Base::get(context, name);
}

LogicalResult AdtType::setBody(ArrayRef<Type> variants) {
  return Base::mutate(variants);
}

bool AdtType::isInitialized() const { return getImpl()->initialized; }

// A single-variant ADT (struct/union) destructures like its variant tuple so it
// participates in SROA/mem2reg. Multi-variant enums overlap their variants in
// memory and are intentionally not destructurable.
static std::optional<TypedTupleType> getSingleVariantTuple(AdtType type) {
  ArrayRef<Type> variants = type.getVariants();
  if (variants.size() != 1)
    return std::nullopt;
  if (auto tuple = llvm::dyn_cast<TypedTupleType>(variants.front()))
    return tuple;
  return std::nullopt;
}

std::optional<llvm::DenseMap<Attribute, Type>>
AdtType::getSubelementIndexMap() const {
  if (std::optional<TypedTupleType> tuple = getSingleVariantTuple(*this))
    return tuple->getSubelementIndexMap();
  return std::nullopt;
}

Type AdtType::getTypeAtIndex(Attribute index) const {
  if (std::optional<TypedTupleType> tuple = getSingleVariantTuple(*this))
    return tuple->getTypeAtIndex(index);
  return {};
}

Type AdtType::parse(AsmParser &parser) {
  std::string name;
  if (parser.parseLess() || parser.parseString(&name))
    return {};

  AdtType type = AdtType::getIdentified(parser.getContext(), name);

  // An optional `, [ <variant-types> ]` body follows. Guard against cycles so a
  // self-referential body is parsed only once.
  if (succeeded(parser.parseOptionalComma())) {
    FailureOr<AsmParser::CyclicParseReset> cyclic =
        parser.tryStartCyclicParse(type);
    if (failed(cyclic)) {
      parser.emitError(parser.getCurrentLocation(),
                       "unexpected recursive ADT body");
      return {};
    }

    SmallVector<Type> variants;
    if (parser.parseLSquare())
      return {};
    if (failed(parser.parseOptionalRSquare())) {
      do {
        Type variant;
        if (parser.parseType(variant))
          return {};
        variants.push_back(variant);
      } while (succeeded(parser.parseOptionalComma()));
      if (parser.parseRSquare())
        return {};
    }
    if (failed(type.setBody(variants))) {
      parser.emitError(parser.getCurrentLocation(),
                       "ADT body conflicts with an existing definition");
      return {};
    }
  }

  if (parser.parseGreater())
    return {};
  return type;
}

void AdtType::print(AsmPrinter &printer) const {
  printer << "<\"" << getName() << "\"";
  // Only emit the body the first time the type is reached; a cyclic back-edge
  // prints just the name reference.
  FailureOr<AsmPrinter::CyclicPrintReset> cyclic =
      printer.tryStartCyclicPrint(*this);
  if (succeeded(cyclic) && isInitialized()) {
    printer << ", [";
    llvm::interleaveComma(getVariants(), printer,
                          [&](Type variant) { printer << variant; });
    printer << "]";
  }
  printer << ">";
}

IntType IntType::getFromSpelling(MLIRContext *context, llvm::StringRef spelling) {
  if (spelling == "isize")
    return get(context, /*width=*/0, /*isSigned=*/true);
  if (spelling == "usize")
    return get(context, /*width=*/0, /*isSigned=*/false);

  bool isSigned;
  if (spelling.consume_front("i"))
    isSigned = true;
  else if (spelling.consume_front("u"))
    isSigned = false;
  else
    return {};

  unsigned width = 0;
  if (spelling.getAsInteger(10, width))
    return {};
  if (width != 8 && width != 16 && width != 32 && width != 64 && width != 128)
    return {};
  return get(context, width, isSigned);
}

std::string IntType::getSpelling() const {
  if (isPointerSized())
    return getIsSigned() ? "isize" : "usize";
  return (llvm::Twine(getIsSigned() ? "i" : "u") + llvm::Twine(getWidth()))
      .str();
}

Type IntType::parse(AsmParser &parser) {
  std::string spelling;
  llvm::SMLoc loc = parser.getCurrentLocation();
  if (parser.parseLess() || parser.parseString(&spelling) ||
      parser.parseGreater())
    return {};

  IntType type = getFromSpelling(parser.getContext(), spelling);
  if (!type) {
    parser.emitError(loc, "unknown Rust integer spelling: ") << spelling;
    return {};
  }
  return type;
}

void IntType::print(AsmPrinter &printer) const {
  printer << "<\"" << getSpelling() << "\">";
}

std::optional<llvm::DenseMap<Attribute, Type>>
TypedTupleType::getSubelementIndexMap() const {
  llvm::DenseMap<Attribute, Type> subelements;
  MLIRContext *context = getContext();
  Type indexType = IntegerType::get(context, 64);
  for (auto [index, elementType] : llvm::enumerate(getElementTypes()))
    subelements[IntegerAttr::get(indexType, index)] = elementType;
  return subelements;
}

Type TypedTupleType::getTypeAtIndex(Attribute index) const {
  auto integerIndex = llvm::dyn_cast<IntegerAttr>(index);
  if (!integerIndex)
    return {};
  int64_t value = integerIndex.getInt();
  ArrayRef<Type> elementTypes = getElementTypes();
  if (value < 0 || static_cast<size_t>(value) >= elementTypes.size())
    return {};
  return elementTypes[value];
}

std::optional<llvm::DenseMap<Attribute, Type>>
TypedArrayType::getSubelementIndexMap() const {
  constexpr uint64_t kMaxDestructurableArrayLength = 1024;
  uint64_t length = getLength();
  if (length > kMaxDestructurableArrayLength)
    return std::nullopt;

  llvm::DenseMap<Attribute, Type> subelements;
  MLIRContext *context = getContext();
  Type indexType = IntegerType::get(context, 64);
  Type elementType = getElementType();
  for (uint64_t index = 0; index < length; ++index)
    subelements[IntegerAttr::get(indexType, static_cast<int64_t>(index))] =
        elementType;
  return subelements;
}

Type TypedArrayType::getTypeAtIndex(Attribute index) const {
  auto integerIndex = llvm::dyn_cast<IntegerAttr>(index);
  if (!integerIndex)
    return {};
  int64_t value = integerIndex.getInt();
  if (value < 0 || static_cast<uint64_t>(value) >= getLength())
    return {};
  return getElementType();
}

void RustMIRDialect::registerTypes() {
  addTypes<
#define GET_TYPEDEF_LIST
#include "mlir/Dialect/RustMIR/IR/RustOpsTypes.cpp.inc"
      >();
}
