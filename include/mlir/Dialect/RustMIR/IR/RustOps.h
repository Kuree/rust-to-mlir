//===- RustOps.h - Rust MIR dialect operations ------------------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_DIALECT_RUST_MLIR_IR_RUSTOPS_H
#define MLIR_DIALECT_RUST_MLIR_IR_RUSTOPS_H

#include "mlir/Dialect/RustMIR/IR/RustMIRDialect.h"
#include "mlir/Dialect/RustMIR/IR/RustTypes.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/MemorySlotInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#define GET_OP_CLASSES
#include "mlir/Dialect/RustMIR/IR/RustOps.h.inc"

#endif // MLIR_DIALECT_RUST_MLIR_IR_RUSTOPS_H
