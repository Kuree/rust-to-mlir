//===- rust-opt.cpp - Rust MLIR optimizer driver ---------------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RustToLLVM/Support/Toolchain.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/Tools/mlir-opt/MlirOptMain.h"

int main(int argc, char **argv) {
  mlir::DialectRegistry registry;
  rust_to_llvm::registerRustToLLVMDialects(registry);
  rust_to_llvm::registerRustToLLVMPasses();
  return mlir::asMainReturnCode(
      mlir::MlirOptMain(argc, argv, "Rust MLIR optimizer driver\n", registry));
}
