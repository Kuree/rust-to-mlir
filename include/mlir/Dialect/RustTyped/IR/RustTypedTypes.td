//===- RustTypedTypes.td - Typed Rust MIR types ------------*- tablegen -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_DIALECT_RUST_TYPED_IR_RUSTTYPEDTYPES_TD
#define MLIR_DIALECT_RUST_TYPED_IR_RUSTTYPEDTYPES_TD

include "mlir/IR/AttrTypeBase.td"
include "mlir/Dialect/RustTyped/IR/RustTypedDialect.td"
include "mlir/Interfaces/MemorySlotInterfaces.td"

class RustTyped_Type<string name, string typeMnemonic, list<Trait> traits = []>
    : TypeDef<RustMIR_Dialect, name, traits> {
  let mnemonic = "typed." # typeMnemonic;
}

def RustTyped_SlotType : RustTyped_Type<"Slot", "slot"> {
  let summary = "Promotable Rust local memory slot";
  let parameters = (ins "Type":$elementType);
  let assemblyFormat = "`<` $elementType `>`";
}

def RustTyped_AddrType : RustTyped_Type<"TypedAddr", "addr"> {
  let summary = "Compiler-internal address of a typed Rust place";
  let parameters = (ins "Type":$elementType);
  let assemblyFormat = "`<` $elementType `>`";
}

def RustTyped_TupleType : RustTyped_Type<"TypedTuple", "tuple",
    [DeclareTypeInterfaceMethods<DestructurableTypeInterface>]> {
  let summary = "Typed Rust aggregate tuple";
  let parameters = (ins ArrayRefParameter<"Type">:$elementTypes);
  let assemblyFormat = "`<` $elementTypes `>`";
  let builders = [
    TypeBuilder<(ins "TypeRange":$elementTypes), [{
      ::llvm::SmallVector<::mlir::Type> types(elementTypes.begin(),
                                              elementTypes.end());
      return $_get($_ctxt, ::llvm::ArrayRef<::mlir::Type>(types));
    }]>
  ];
}

def RustTyped_ArrayType : RustTyped_Type<"TypedArray", "array",
    [DeclareTypeInterfaceMethods<DestructurableTypeInterface>]> {
  let summary = "Typed Rust fixed-size array";
  let parameters = (ins "Type":$elementType, "uint64_t":$length);
  let assemblyFormat = "`<` $elementType `,` $length `>`";
}

def RustTyped_RefType : RustTyped_Type<"TypedRef", "ref"> {
  let summary = "Typed Rust reference";
  let parameters = (ins StringRefParameter<"Rust reference mutability">:$mutability,
                        "Type":$pointeeType);
  let assemblyFormat = "`<` $mutability `,` $pointeeType `>`";
}

def RustTyped_RawPtrType : RustTyped_Type<"TypedRawPtr", "rawptr"> {
  let summary = "Typed Rust raw pointer";
  let parameters = (ins StringRefParameter<"Rust raw pointer mutability">:$mutability,
                        "Type":$pointeeType);
  let assemblyFormat = "`<` $mutability `,` $pointeeType `>`";
}

#endif // MLIR_DIALECT_RUST_TYPED_IR_RUSTTYPEDTYPES_TD
