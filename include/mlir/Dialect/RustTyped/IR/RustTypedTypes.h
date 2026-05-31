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
using AddrType = ::mlir::rust::mir::TypedAddrType;
using TupleType = ::mlir::rust::mir::TypedTupleType;
using ArrayType = ::mlir::rust::mir::TypedArrayType;
using SliceType = ::mlir::rust::mir::TypedSliceType;
using RefType = ::mlir::rust::mir::TypedRefType;
using RawPtrType = ::mlir::rust::mir::TypedRawPtrType;
} // namespace mlir::rust::typed

#endif // MLIR_DIALECT_RUST_TYPED_IR_RUSTTYPEDTYPES_H
