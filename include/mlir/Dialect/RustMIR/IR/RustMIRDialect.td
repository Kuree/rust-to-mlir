//===- RustMIRDialect.td - Rust MIR dialect -------------------*- tablegen -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_DIALECT_RUSTMIR_IR_RUSTDIALECT_TD
#define MLIR_DIALECT_RUSTMIR_IR_RUSTDIALECT_TD

include "mlir/IR/OpBase.td"

def RustMIR_Dialect : Dialect {
  let name = "rust";
  let summary = "Rust MIR dialect imported from rustc_public";
  let description = [{
    The rust dialect owns the rust.mir mirror layer and the rust.typed lowering
    layer. MLIR dialect namespaces cannot contain '.' characters, so the MIR and
    typed layers are represented as operation and type mnemonic prefixes.
  }];
  let cppNamespace = "::mlir::rust::mir";

  let useDefaultTypePrinterParser = 1;
  let extraClassDeclaration = [{
    void registerTypes();
  }];
}

class Rust_Op<string mnemonic, list<Trait> traits = []> :
    Op<RustMIR_Dialect, "mir." # mnemonic, traits>;

#endif // MLIR_DIALECT_RUSTMIR_IR_RUSTDIALECT_TD
