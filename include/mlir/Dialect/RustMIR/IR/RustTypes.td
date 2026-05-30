//===- RustTypes.td - Rust MIR dialect types ---------------*- tablegen -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_DIALECT_RUSTMIR_IR_RUSTTYPES_TD
#define MLIR_DIALECT_RUSTMIR_IR_RUSTTYPES_TD

include "mlir/IR/AttrTypeBase.td"
include "mlir/Dialect/RustMIR/IR/RustAttrs.td"
include "mlir/Dialect/RustMIR/IR/RustMIRDialect.td"

class Rust_Type<string name, string typeMnemonic, list<Trait> traits = []>
    : TypeDef<RustMIR_Dialect, name, traits> {
  let mnemonic = "mir." # typeMnemonic;
}

def Rust_BoolType : Rust_Type<"Bool", "bool"> {
  let summary = "Rust bool type";
}

def Rust_CharType : Rust_Type<"Char", "char"> {
  let summary = "Rust char type";
}

def Rust_UnitType : Rust_Type<"Unit", "unit"> {
  let summary = "Rust unit type";
}

def Rust_NeverType : Rust_Type<"Never", "never"> {
  let summary = "Rust never type";
}

def Rust_IntType : Rust_Type<"Int", "int"> {
  let summary = "Rust integer type";
  let parameters = (ins StringRefParameter<"rust integer spelling">:$spelling);
  let assemblyFormat = "`<` $spelling `>`";
}

def Rust_TupleType : Rust_Type<"Tuple", "tuple"> {
  let summary = "Rust tuple type";
  let parameters = (ins StringRefParameter<"rust tuple spelling">:$spelling);
  let assemblyFormat = "`<` $spelling `>`";
}

def Rust_ArrayType : Rust_Type<"Array", "array"> {
  let summary = "Rust array type";
  let parameters = (ins StringRefParameter<"rust array spelling">:$spelling);
  let assemblyFormat = "`<` $spelling `>`";
}

def Rust_SliceType : Rust_Type<"Slice", "slice"> {
  let summary = "Rust slice type";
  let parameters = (ins StringRefParameter<"rust slice spelling">:$spelling);
  let assemblyFormat = "`<` $spelling `>`";
}

def Rust_RawPtrType : Rust_Type<"RawPtr", "rawptr"> {
  let summary = "Rust raw pointer type";
  let parameters = (ins StringRefParameter<"rust raw pointer spelling">:$spelling);
  let assemblyFormat = "`<` $spelling `>`";
}

def Rust_RefType : Rust_Type<"Ref", "ref"> {
  let summary = "Rust reference type";
  let parameters = (ins StringRefParameter<"rust reference spelling">:$spelling);
  let assemblyFormat = "`<` $spelling `>`";
}

def Rust_FnType : Rust_Type<"Fn", "fn"> {
  let summary = "Rust function type";
  let parameters = (ins StringRefParameter<"rust function type spelling">:$spelling);
  let assemblyFormat = "`<` $spelling `>`";
}

def Rust_AdtType : Rust_Type<"Adt", "adt"> {
  let summary = "Rust ADT type";
  let parameters = (ins StringRefParameter<"rust ADT spelling">:$spelling);
  let assemblyFormat = "`<` $spelling `>`";
}

def Rust_OpaqueType : Rust_Type<"Opaque", "opaque"> {
  let summary = "Opaque Rust type";
  let parameters = (ins StringRefParameter<"rust type debug spelling">:$spelling);
  let assemblyFormat = "`<` $spelling `>`";
}

include "mlir/Dialect/RustTyped/IR/RustTypedTypes.td"

#endif // MLIR_DIALECT_RUSTMIR_IR_RUSTTYPES_TD
