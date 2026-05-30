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

#endif // MLIR_DIALECT_RUST_TYPED_IR_RUSTTYPEDTYPES_TD
