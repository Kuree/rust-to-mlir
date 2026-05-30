//===- Passes.h - Rust MIR dialect passes -----------------------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_DIALECT_RUSTMIR_TRANSFORMS_PASSES_H
#define MLIR_DIALECT_RUSTMIR_TRANSFORMS_PASSES_H

#include <memory>

namespace mlir {
class Pass;

namespace rust {
std::unique_ptr<Pass> createLiftTypedMIRPass();
void registerRustMIRPasses();
} // namespace rust
} // namespace mlir

#endif // MLIR_DIALECT_RUSTMIR_TRANSFORMS_PASSES_H
