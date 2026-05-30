//===- Passes.td - Rust typed to arith pass definitions ----*- tablegen -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_CONVERSION_RUSTTYPEDTOARITH_PASSES
#define MLIR_CONVERSION_RUSTTYPEDTOARITH_PASSES

include "mlir/Pass/PassBase.td"

def ConvertRustTypedToArithPass
    : Pass<"convert-rust-typed-to-arith", "ModuleOp"> {
  let summary = "Convert rust.typed scalar operations to arith";
  let description = [{
    Lowers Rust typed integer and boolean constants, unary operations, and
    binary operations to the arith dialect. Rust `usize` and `isize` are lowered
    using the module data layout, falling back to the rustc target pointer width
    attribute emitted by the Rust frontend.
  }];
  let constructor = "mlir::createConvertRustTypedToArithPass()";
  let dependentDialects = [
    "arith::ArithDialect",
    "rust::mir::RustMIRDialect",
    "ub::UBDialect"
  ];
}

#endif // MLIR_CONVERSION_RUSTTYPEDTOARITH_PASSES
