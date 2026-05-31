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

#define GET_TYPEDEF_CLASSES
#include "mlir/Dialect/RustMIR/IR/RustOpsTypes.cpp.inc"

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
