//===- rust-opt.cpp - Rust MLIR optimizer driver ---------------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RustToMLIR/Support/Toolchain.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/Tools/mlir-opt/MlirOptMain.h"

int main(int argc, char **argv) {
  mlir::DialectRegistry registry;
  rust_to_mlir::registerRustToMLIRDialects(registry);
  rust_to_mlir::registerRustToMLIRPasses();
  return mlir::asMainReturnCode(
      mlir::MlirOptMain(argc, argv, "Rust MLIR optimizer driver\n", registry));
}
