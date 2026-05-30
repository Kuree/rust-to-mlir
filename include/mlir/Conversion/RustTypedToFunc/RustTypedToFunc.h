//===- RustTypedToFunc.h - Rust typed to func conversion -------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_CONVERSION_RUSTTYPEDTOFUNC_RUSTTYPEDTOFUNC_H
#define MLIR_CONVERSION_RUSTTYPEDTOFUNC_RUSTTYPEDTOFUNC_H

#include "mlir/Pass/Pass.h"

#include <memory>

namespace mlir {

std::unique_ptr<Pass> createConvertRustTypedToFuncPass();

#define GEN_PASS_DECL_CONVERTRUSTTYPEDTOFUNCPASS
#define GEN_PASS_REGISTRATION
#include "mlir/Conversion/RustTypedToFunc/RustTypedToFuncPasses.h.inc"

} // namespace mlir

#endif // MLIR_CONVERSION_RUSTTYPEDTOFUNC_RUSTTYPEDTOFUNC_H
