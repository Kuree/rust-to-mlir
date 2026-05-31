//===- RustTypes.h - Rust MIR dialect types ---------------------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_DIALECT_RUST_MLIR_IR_RUSTTYPES_H
#define MLIR_DIALECT_RUST_MLIR_IR_RUSTTYPES_H

#include "mlir/IR/BuiltinTypes.h"
#include "mlir/Interfaces/MemorySlotInterfaces.h"

#include "mlir/Dialect/RustMIR/IR/RustEnums.h.inc"

// Hand-written storage backing the mutable, recursion-capable AdtType (defined
// in RustTypes.cpp). Forward-declared here so the generated type class can name
// it as its storage class.
namespace mlir::rust::mir::detail {
struct AdtTypeStorage;
} // namespace mlir::rust::mir::detail

#define GET_ATTRDEF_CLASSES
#include "mlir/Dialect/RustMIR/IR/RustAttrs.h.inc"

#define GET_TYPEDEF_CLASSES
#include "mlir/Dialect/RustMIR/IR/RustOpsTypes.h.inc"

#endif // MLIR_DIALECT_RUST_MLIR_IR_RUSTTYPES_H
