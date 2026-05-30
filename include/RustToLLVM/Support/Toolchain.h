//===- Toolchain.h - RustToLLVM tool setup helpers -------------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef RUSTTOLLVM_SUPPORT_TOOLCHAIN_H
#define RUSTTOLLVM_SUPPORT_TOOLCHAIN_H

#include "mlir/Support/LogicalResult.h"

namespace mlir {
class DialectRegistry;
class OpPassManager;
class Operation;
} // namespace mlir

namespace rust_to_llvm {

struct RustToLLVMLoweringOptions {
  bool eraseSourceMIR = true;
};

void registerRustToLLVMDialects(mlir::DialectRegistry &registry);
void registerRustToLLVMPasses();
void registerRustToLLVMIRTranslations(mlir::DialectRegistry &registry);

void populateRustToLLVMLoweringPipeline(
    mlir::OpPassManager &pm, const RustToLLVMLoweringOptions &options = {});

mlir::LogicalResult
lowerRustToLLVM(mlir::Operation *op,
                const RustToLLVMLoweringOptions &options = {});

} // namespace rust_to_llvm

#endif // RUSTTOLLVM_SUPPORT_TOOLCHAIN_H
