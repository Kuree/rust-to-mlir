//===- RustTypedTypes.h - Typed Rust MIR types -----------------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_DIALECT_RUST_TYPED_IR_RUSTTYPEDTYPES_H
#define MLIR_DIALECT_RUST_TYPED_IR_RUSTTYPEDTYPES_H

#include "mlir/Dialect/RustMIR/IR/RustTypes.h"

namespace mlir::rust::typed {
using SlotType = ::mlir::rust::mir::SlotType;
using TupleType = ::mlir::rust::mir::TypedTupleType;
} // namespace mlir::rust::typed

#endif // MLIR_DIALECT_RUST_TYPED_IR_RUSTTYPEDTYPES_H
