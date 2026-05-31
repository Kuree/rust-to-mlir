//===- Toolchain.h - RustToMLIR tool setup helpers -------------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef RUSTTOMLIR_SUPPORT_TOOLCHAIN_H
#define RUSTTOMLIR_SUPPORT_TOOLCHAIN_H

#include "mlir/Support/LogicalResult.h"

namespace mlir {
class DialectRegistry;
class OpPassManager;
class Operation;
} // namespace mlir

namespace rust_to_mlir {

struct RustToLLVMLoweringOptions {
  bool eraseSourceMIR = true;
};

void registerRustToMLIRDialects(mlir::DialectRegistry &registry);
void registerRustToMLIRPasses();
void registerRustToMLIRLLVMIRTranslations(mlir::DialectRegistry &registry);

void populateRustToLLVMLoweringPipeline(
    mlir::OpPassManager &pm, const RustToLLVMLoweringOptions &options = {});

mlir::LogicalResult
lowerRustToLLVM(mlir::Operation *op,
                const RustToLLVMLoweringOptions &options = {});

} // namespace rust_to_mlir

#endif // RUSTTOMLIR_SUPPORT_TOOLCHAIN_H
