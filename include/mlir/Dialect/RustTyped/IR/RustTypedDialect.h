//===- RustTypedDialect.h - Typed Rust MIR dialect -------------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_DIALECT_RUST_TYPED_IR_RUSTTYPEDDIALECT_H
#define MLIR_DIALECT_RUST_TYPED_IR_RUSTTYPEDDIALECT_H

#include "mlir/Dialect/RustMIR/IR/RustMIRDialect.h"

namespace mlir::rust::typed {
using RustTypedDialect = ::mlir::rust::mir::RustMIRDialect;
} // namespace mlir::rust::typed

#endif // MLIR_DIALECT_RUST_TYPED_IR_RUSTTYPEDDIALECT_H
