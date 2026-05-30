//===- Passes.td - Rust MIR transform pass definitions -----*- tablegen -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_DIALECT_RUSTMIR_TRANSFORMS_PASSES
#define MLIR_DIALECT_RUSTMIR_TRANSFORMS_PASSES

include "mlir/Pass/PassBase.td"

def LiftTypedMIRPass : Pass<"rust-lift-typed-mir", "ModuleOp"> {
  let summary = "Lift supported rust.mir mirror ops into typed rust.typed ops";
  let description = [{
    Lifts supported Rust MIR mirror operations into typed rust.typed operations
    while preserving the source MIR by default for inspection and testing.
  }];
  let constructor = "mlir::rust::createLiftTypedMIRPass()";
  let dependentDialects = [
    "rust::mir::RustMIRDialect"
  ];
  let options = [
    Option<"eraseSourceMIR", "erase-source-mir", "bool", /*default=*/"false",
           "Erase source rust.mir.func ops after successful lift">,
  ];
}

#endif // MLIR_DIALECT_RUSTMIR_TRANSFORMS_PASSES
