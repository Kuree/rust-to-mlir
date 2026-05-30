//===- Passes.td - Rust typed to func pass definitions ------*- tablegen -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_CONVERSION_RUSTTYPEDTOFUNC_PASSES
#define MLIR_CONVERSION_RUSTTYPEDTOFUNC_PASSES

include "mlir/Pass/PassBase.td"

def ConvertRustTypedToFuncPass
    : Pass<"convert-rust-typed-to-func", "ModuleOp"> {
  let summary = "Convert rust.typed.func containers to func.func";
  let description = [{
    Converts the Rust typed function container operation to the builtin func
    dialect while preserving the typed Rust body operations for later lowering.
  }];
  let constructor = "mlir::createConvertRustTypedToFuncPass()";
  let dependentDialects = [
    "func::FuncDialect",
    "rust::mir::RustMIRDialect"
  ];
}

#endif // MLIR_CONVERSION_RUSTTYPEDTOFUNC_PASSES
