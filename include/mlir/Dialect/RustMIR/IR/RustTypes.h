//===- RustTypes.h - Rust MIR dialect types ---------------------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_DIALECT_RUST_MLIR_IR_RUSTTYPES_H
#define MLIR_DIALECT_RUST_MLIR_IR_RUSTTYPES_H

#include "mlir/IR/BuiltinTypes.h"
#include "mlir/Interfaces/MemorySlotInterfaces.h"

#define GET_TYPEDEF_CLASSES
#include "mlir/Dialect/RustMIR/IR/RustOpsTypes.h.inc"

#endif // MLIR_DIALECT_RUST_MLIR_IR_RUSTTYPES_H
