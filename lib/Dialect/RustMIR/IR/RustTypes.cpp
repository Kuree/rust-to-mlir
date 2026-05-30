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
