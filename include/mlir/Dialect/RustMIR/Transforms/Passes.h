//===- Passes.h - Rust MIR dialect passes -----------------------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_DIALECT_RUSTMIR_TRANSFORMS_PASSES_H
#define MLIR_DIALECT_RUSTMIR_TRANSFORMS_PASSES_H

#include "mlir/Pass/Pass.h"

#include <memory>

namespace mlir {
class Pass;

namespace rust {
std::unique_ptr<Pass> createLiftTypedMIRPass();
void registerRustMIRPasses();
} // namespace rust

#define GEN_PASS_DECL_LIFTTYPEDMIRPASS
#define GEN_PASS_REGISTRATION
#include "mlir/Dialect/RustMIR/Transforms/RustMIRTransformsPasses.h.inc"
} // namespace mlir

#endif // MLIR_DIALECT_RUSTMIR_TRANSFORMS_PASSES_H
