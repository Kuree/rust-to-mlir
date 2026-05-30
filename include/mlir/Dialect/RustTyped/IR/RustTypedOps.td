//===- RustTypedOps.td - Typed Rust MIR operations ----------*- tablegen -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_DIALECT_RUST_TYPED_IR_RUSTTYPEDOPS_TD
#define MLIR_DIALECT_RUST_TYPED_IR_RUSTTYPEDOPS_TD

include "mlir/Dialect/RustTyped/IR/RustTypedTypes.td"
include "mlir/IR/SymbolInterfaces.td"
include "mlir/Interfaces/MemorySlotInterfaces.td"
include "mlir/Interfaces/SideEffectInterfaces.td"

def RustTyped_TypedFuncOp : RustTyped_Op<"func",
    [IsolatedFromAbove, NoTerminator, SingleBlock, Symbol]> {
  let summary = "Typed Rust MIR function body";
  let arguments = (ins SymbolNameAttr:$sym_name,
                       OptionalAttr<StrAttr>:$rust_name,
                       OptionalAttr<StrAttr>:$signature,
                       OptionalAttr<I64Attr>:$arg_count,
                       OptionalAttr<StrAttr>:$span);
  let regions = (region SizedRegion<1>:$body);
  let assemblyFormat = "$sym_name attr-dict-with-keyword $body";
}

def RustTyped_TypedBlockOp : RustTyped_Op<"block", [NoTerminator, SingleBlock]> {
  let summary = "Typed Rust MIR basic block";
  let arguments = (ins I64Attr:$index, OptionalAttr<StrAttr>:$span);
  let regions = (region SizedRegion<1>:$body);
  let assemblyFormat = "$index attr-dict-with-keyword $body";
}

def RustTyped_LocalSlotOp : RustTyped_Op<"local_slot",
    [DeclareOpInterfaceMethods<PromotableAllocationOpInterface>,
     DeclareOpInterfaceMethods<DestructurableAllocationOpInterface>]> {
  let summary = "Typed MIR local memory slot";
  let arguments = (ins I64Attr:$index,
                       OptionalAttr<StrAttr>:$name,
                       OptionalAttr<StrAttr>:$mutability,
                       OptionalAttr<StrAttr>:$role,
                       OptionalAttr<StrAttr>:$span);
  let results = (outs RustTyped_SlotType:$slot);
  let assemblyFormat = "attr-dict `:` type($slot)";
}

def RustTyped_LoadOp : RustTyped_Op<"load",
    [DeclareOpInterfaceMethods<PromotableMemOpInterface>,
     DeclareOpInterfaceMethods<DestructurableAccessorOpInterface>]> {
  let summary = "Load from a typed Rust local slot";
  let arguments = (ins RustTyped_SlotType:$slot);
  let results = (outs AnyType:$value);
  let assemblyFormat = "$slot attr-dict `:` type($slot) `->` type($value)";
  let hasVerifier = 1;
}

def RustTyped_StoreOp : RustTyped_Op<"store",
    [DeclareOpInterfaceMethods<PromotableMemOpInterface>,
     DeclareOpInterfaceMethods<DestructurableAccessorOpInterface>]> {
  let summary = "Store to a typed Rust local slot";
  let arguments = (ins AnyType:$value, RustTyped_SlotType:$slot);
  let assemblyFormat = "$value `,` $slot attr-dict `:` type($value) `,` type($slot)";
  let hasVerifier = 1;
}

def RustTyped_TypedConstOp : RustTyped_Op<"const"> {
  let summary = "Typed Rust MIR constant";
  let arguments = (ins OptionalAttr<AnyAttr>:$value,
                       OptionalAttr<StrAttr>:$debug,
                       OptionalAttr<StrAttr>:$span);
  let results = (outs AnyType:$result);
  let assemblyFormat = "attr-dict `:` type($result)";
}

def RustTyped_BinOp : RustTyped_Op<"binop"> {
  let summary = "Typed Rust MIR binary operation";
  let arguments = (ins AnyType:$lhs, AnyType:$rhs, StrAttr:$op,
                       OptionalAttr<StrAttr>:$span);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$lhs `,` $rhs attr-dict `:` type($lhs) `,` type($rhs) `->` type($result)";
}

def RustTyped_UnOp : RustTyped_Op<"unop"> {
  let summary = "Typed Rust MIR unary operation";
  let arguments = (ins AnyType:$input, StrAttr:$op,
                       OptionalAttr<StrAttr>:$span);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$input attr-dict `:` type($input) `->` type($result)";
}

def RustTyped_MakeTupleOp : RustTyped_Op<"aggregate"> {
  let summary = "Build a typed Rust tuple aggregate";
  let arguments = (ins Variadic<AnyType>:$values,
                       OptionalAttr<StrAttr>:$span);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$values attr-dict `:` type($values) `->` type($result)";
}

def RustTyped_FieldOp : RustTyped_Op<"field"> {
  let summary = "Extract a typed Rust tuple field";
  let arguments = (ins AnyType:$aggregate, I64Attr:$index,
                       OptionalAttr<StrAttr>:$span);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$aggregate attr-dict `:` type($aggregate) `->` type($result)";
}

def RustTyped_TypedGotoOp : RustTyped_Op<"goto"> {
  let summary = "Typed Rust MIR goto terminator placeholder";
  let arguments = (ins I64Attr:$target, OptionalAttr<StrAttr>:$span);
  let assemblyFormat = "attr-dict";
}

def RustTyped_TypedSwitchIntOp : RustTyped_Op<"switch_int"> {
  let summary = "Typed Rust MIR switch-int terminator placeholder";
  let arguments = (ins AnyInteger:$discr, OptionalAttr<DictionaryAttr>:$targets,
                       OptionalAttr<StrAttr>:$span);
  let assemblyFormat = "$discr attr-dict `:` type($discr)";
}

def RustTyped_TypedReturnOp : RustTyped_Op<"return"> {
  let summary = "Typed Rust MIR return";
  let arguments = (ins Variadic<AnyType>:$values, OptionalAttr<StrAttr>:$span);
  let assemblyFormat = "attr-dict ($values^ `:` type($values))?";
}

#endif // MLIR_DIALECT_RUST_TYPED_IR_RUSTTYPEDOPS_TD
