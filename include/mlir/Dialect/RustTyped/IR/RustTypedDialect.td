//===- RustTypedDialect.td - Typed Rust MIR dialect ----------*- tablegen -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_DIALECT_RUST_TYPED_IR_RUSTTYPEDDIALECT_TD
#define MLIR_DIALECT_RUST_TYPED_IR_RUSTTYPEDDIALECT_TD

include "mlir/Dialect/RustMIR/IR/RustMIRDialect.td"

class RustTyped_Op<string mnemonic, list<Trait> traits = []> :
    Op<RustMIR_Dialect, "typed." # mnemonic, traits>;

#endif // MLIR_DIALECT_RUST_TYPED_IR_RUSTTYPEDDIALECT_TD
