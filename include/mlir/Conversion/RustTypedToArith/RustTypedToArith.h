//===- RustTypedToArith.h - Rust typed to arith conversion -----*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_CONVERSION_RUSTTYPEDTOARITH_RUSTTYPEDTOARITH_H
#define MLIR_CONVERSION_RUSTTYPEDTOARITH_RUSTTYPEDTOARITH_H

#include "mlir/Pass/Pass.h"

#include <memory>

namespace mlir {
class Pass;

std::unique_ptr<Pass> createConvertRustTypedToArithPass();

#define GEN_PASS_DECL_CONVERTRUSTTYPEDTOARITHPASS
#define GEN_PASS_REGISTRATION
#include "mlir/Conversion/RustTypedToArith/RustTypedToArithPasses.h.inc"
} // namespace mlir

#endif // MLIR_CONVERSION_RUSTTYPEDTOARITH_RUSTTYPEDTOARITH_H
