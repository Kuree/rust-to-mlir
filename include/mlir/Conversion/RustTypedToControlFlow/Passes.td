//===- Passes.td - Rust typed to cf pass definitions -------*- tablegen -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_CONVERSION_RUSTTYPEDTOCONTROLFLOW_PASSES
#define MLIR_CONVERSION_RUSTTYPEDTOCONTROLFLOW_PASSES

include "mlir/Pass/PassBase.td"

def ConvertRustTypedToControlFlowPass
    : Pass<"convert-rust-typed-to-cf", "ModuleOp"> {
  let summary = "Convert nested rust.typed.block regions to cf control flow";
  let description = [{
    Lowers Rust typed basic-block container operations and typed terminator
    placeholders to func/cf control flow so CFG-sensitive MLIR passes can run.
  }];
  let constructor = "mlir::createConvertRustTypedToControlFlowPass()";
  let dependentDialects = [
    "cf::ControlFlowDialect",
    "func::FuncDialect",
    "rust::mir::RustMIRDialect"
  ];
}

#endif // MLIR_CONVERSION_RUSTTYPEDTOCONTROLFLOW_PASSES
