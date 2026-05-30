//===- Passes.td - Rust typed memory to LLVM pass definitions -*- tablegen -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_CONVERSION_RUSTTYPEDMEMORYTOLLVM_PASSES
#define MLIR_CONVERSION_RUSTTYPEDMEMORYTOLLVM_PASSES

include "mlir/Pass/PassBase.td"

def ConvertRustTypedMemoryToLLVMPass
    : Pass<"convert-rust-typed-memory-to-llvm", "ModuleOp"> {
  let summary = "Convert rust.typed memory and pointer operations to LLVM";
  let description = [{
    Lowers surviving Rust typed local slots and pointer-producing operations to
    the LLVM dialect. This pass is intended to run after Rust scalar lowering and
    after SROA/mem2reg so only escaping or otherwise unpromoted slots become
    concrete LLVM stack allocations.
  }];
  let constructor = "mlir::createConvertRustTypedMemoryToLLVMPass()";
  let dependentDialects = [
    "LLVM::LLVMDialect",
    "func::FuncDialect",
    "rust::mir::RustMIRDialect"
  ];
}

#endif // MLIR_CONVERSION_RUSTTYPEDMEMORYTOLLVM_PASSES
