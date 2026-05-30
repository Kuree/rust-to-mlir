//===- RustTypedOps.h - Typed Rust MIR operations --------------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_DIALECT_RUST_TYPED_IR_RUSTTYPEDOPS_H
#define MLIR_DIALECT_RUST_TYPED_IR_RUSTTYPEDOPS_H

#include "mlir/Dialect/RustTyped/IR/RustTypedDialect.h"
#include "mlir/Dialect/RustTyped/IR/RustTypedTypes.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/MemorySlotInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#define GET_OP_CLASSES
#include "mlir/Dialect/RustTyped/IR/RustTypedOps.h.inc"

#endif // MLIR_DIALECT_RUST_TYPED_IR_RUSTTYPEDOPS_H
