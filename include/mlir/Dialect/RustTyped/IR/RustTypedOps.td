//===- RustTypedOps.td - Typed Rust MIR operations ----------*- tablegen -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_DIALECT_RUST_TYPED_IR_RUSTTYPEDOPS_TD
#define MLIR_DIALECT_RUST_TYPED_IR_RUSTTYPEDOPS_TD

include "mlir/Dialect/RustTyped/IR/RustTypedTypes.td"
include "mlir/Dialect/RustMIR/IR/RustAttrs.td"
include "mlir/IR/SymbolInterfaces.td"
include "mlir/Interfaces/MemorySlotInterfaces.td"
include "mlir/Interfaces/SideEffectInterfaces.td"

def RustTyped_AddressLikeType : AnyTypeOf<[
  RustTyped_SlotType,
  RustTyped_AddrType,
  RustTyped_RefType,
  RustTyped_RawPtrType
]>;

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
                       OptionalAttr<RustLocalMutabilityAttr>:$mutability,
                       OptionalAttr<RustLocalRoleAttr>:$role,
                       OptionalAttr<StrAttr>:$span,
                       OptionalAttr<BoolAttr>:$address_taken);
  let results = (outs RustTyped_SlotType:$slot);
  let assemblyFormat = "attr-dict `:` type($slot)";
}

def RustTyped_LoadOp : RustTyped_Op<"load",
    [DeclareOpInterfaceMethods<PromotableMemOpInterface>,
     DeclareOpInterfaceMethods<DestructurableAccessorOpInterface>]> {
  let summary = "Load from a typed Rust place address";
  let arguments = (ins RustTyped_AddressLikeType:$slot);
  let results = (outs AnyType:$value);
  let assemblyFormat = "$slot attr-dict `:` type($slot) `->` type($value)";
  let hasVerifier = 1;
}

def RustTyped_StoreOp : RustTyped_Op<"store",
    [DeclareOpInterfaceMethods<PromotableMemOpInterface>,
     DeclareOpInterfaceMethods<DestructurableAccessorOpInterface>]> {
  let summary = "Store to a typed Rust place address";
  let arguments = (ins AnyType:$value, RustTyped_AddressLikeType:$slot);
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

class RustTyped_BinaryOp<string mnemonic, string opSummary>
    : RustTyped_Op<mnemonic> {
  let summary = opSummary;
  let arguments = (ins AnyType:$lhs, AnyType:$rhs,
                       OptionalAttr<StrAttr>:$span);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$lhs `,` $rhs attr-dict `:` type($lhs) `,` type($rhs) `->` type($result)";
}

def RustTyped_AddOp : RustTyped_BinaryOp<"add", "Typed Rust MIR add">;
def RustTyped_AddUncheckedOp
    : RustTyped_BinaryOp<"add_unchecked", "Typed Rust MIR unchecked add">;
def RustTyped_SubOp : RustTyped_BinaryOp<"sub", "Typed Rust MIR sub">;
def RustTyped_SubUncheckedOp
    : RustTyped_BinaryOp<"sub_unchecked", "Typed Rust MIR unchecked sub">;
def RustTyped_MulOp : RustTyped_BinaryOp<"mul", "Typed Rust MIR mul">;
def RustTyped_MulUncheckedOp
    : RustTyped_BinaryOp<"mul_unchecked", "Typed Rust MIR unchecked mul">;
def RustTyped_DivOp : RustTyped_BinaryOp<"div", "Typed Rust MIR div">;
def RustTyped_RemOp : RustTyped_BinaryOp<"rem", "Typed Rust MIR rem">;
def RustTyped_BitAndOp : RustTyped_BinaryOp<"bit_and", "Typed Rust MIR bit and">;
def RustTyped_BitOrOp : RustTyped_BinaryOp<"bit_or", "Typed Rust MIR bit or">;
def RustTyped_BitXorOp : RustTyped_BinaryOp<"bit_xor", "Typed Rust MIR bit xor">;
def RustTyped_ShlOp : RustTyped_BinaryOp<"shl", "Typed Rust MIR shift left">;
def RustTyped_ShlUncheckedOp
    : RustTyped_BinaryOp<"shl_unchecked", "Typed Rust MIR unchecked shift left">;
def RustTyped_ShrOp : RustTyped_BinaryOp<"shr", "Typed Rust MIR shift right">;
def RustTyped_ShrUncheckedOp
    : RustTyped_BinaryOp<"shr_unchecked", "Typed Rust MIR unchecked shift right">;
def RustTyped_EqOp : RustTyped_BinaryOp<"eq", "Typed Rust MIR equal">;
def RustTyped_NeOp : RustTyped_BinaryOp<"ne", "Typed Rust MIR not equal">;
def RustTyped_LtOp : RustTyped_BinaryOp<"lt", "Typed Rust MIR less than">;
def RustTyped_LeOp : RustTyped_BinaryOp<"le", "Typed Rust MIR less or equal">;
def RustTyped_GtOp : RustTyped_BinaryOp<"gt", "Typed Rust MIR greater than">;
def RustTyped_GeOp : RustTyped_BinaryOp<"ge", "Typed Rust MIR greater or equal">;

class RustTyped_CheckedBinaryOp<string mnemonic, string opSummary>
    : RustTyped_Op<mnemonic> {
  let summary = opSummary;
  let arguments = (ins AnyType:$lhs, AnyType:$rhs,
                       OptionalAttr<StrAttr>:$span);
  let results = (outs AnyType:$value, AnyType:$overflow);
  let assemblyFormat = "$lhs `,` $rhs attr-dict `:` type($lhs) `,` type($rhs) `->` type($value) `,` type($overflow)";
}

def RustTyped_CheckedAddOp
    : RustTyped_CheckedBinaryOp<"checked_add", "Typed Rust MIR checked add">;
def RustTyped_CheckedSubOp
    : RustTyped_CheckedBinaryOp<"checked_sub", "Typed Rust MIR checked sub">;
def RustTyped_CheckedMulOp
    : RustTyped_CheckedBinaryOp<"checked_mul", "Typed Rust MIR checked mul">;

class RustTyped_UnaryOp<string mnemonic, string opSummary>
    : RustTyped_Op<mnemonic> {
  let summary = opSummary;
  let arguments = (ins AnyType:$input,
                       OptionalAttr<StrAttr>:$span);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$input attr-dict `:` type($input) `->` type($result)";
}

def RustTyped_NegOp : RustTyped_UnaryOp<"neg", "Typed Rust MIR negation">;
def RustTyped_NotOp : RustTyped_UnaryOp<"not", "Typed Rust MIR bitwise not">;

def RustTyped_IntCastOp : RustTyped_Op<"int_cast"> {
  let summary = "Typed Rust MIR integer cast";
  let arguments = (ins AnyType:$input,
                       OptionalAttr<StrAttr>:$span);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$input attr-dict `:` type($input) `->` type($result)";
  let hasVerifier = 1;
}

def RustTyped_NumericCastOp : RustTyped_Op<"numeric_cast"> {
  let summary = "Typed Rust MIR numeric cast";
  let arguments = (ins RustCastKindAttr:$cast_kind,
                       AnyType:$input,
                       OptionalAttr<StrAttr>:$span);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$input attr-dict `:` type($input) `->` type($result)";
  let hasVerifier = 1;
}

def RustTyped_MakeAggregateOp : RustTyped_Op<"aggregate"> {
  let summary = "Build a typed Rust aggregate";
  let arguments = (ins Variadic<AnyType>:$values,
                       OptionalAttr<I64Attr>:$variant_index,
                       OptionalAttr<StrAttr>:$discriminant,
                       OptionalAttr<StrAttr>:$span);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$values attr-dict `:` type($values) `->` type($result)";
}

def RustTyped_FieldOp : RustTyped_Op<"field"> {
  let summary = "Extract a typed Rust indexed aggregate element";
  let arguments = (ins AnyType:$aggregate, I64Attr:$index,
                       OptionalAttr<I64Attr>:$variant_index,
                       OptionalAttr<StrAttr>:$span);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$aggregate attr-dict `:` type($aggregate) `->` type($result)";
}

def RustTyped_FieldAddrOp : RustTyped_Op<"field_addr"> {
  let summary = "Compute the address of a typed Rust indexed aggregate element";
  let arguments = (ins AnyType:$base, I64Attr:$index,
                       OptionalAttr<I64Attr>:$variant_index,
                       OptionalAttr<StrAttr>:$span);
  let results = (outs RustTyped_AddrType:$address);
  let assemblyFormat = "$base attr-dict `:` type($base) `->` type($address)";
  let hasVerifier = 1;
}

def RustTyped_TypedDiscriminantOp : RustTyped_Op<"discriminant"> {
  let summary = "Read a typed Rust enum discriminant";
  let arguments = (ins AnyType:$aggregate,
                       OptionalAttr<StrAttr>:$span);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$aggregate attr-dict `:` type($aggregate) `->` type($result)";
}

def RustTyped_IndexAddrOp : RustTyped_Op<"index_addr"> {
  let summary = "Compute the address of a typed Rust dynamic array or slice element";
  let arguments = (ins AnyType:$base, AnyType:$index,
                       OptionalAttr<StrAttr>:$span);
  let results = (outs RustTyped_AddrType:$address);
  let assemblyFormat = "$base `[` $index `]` attr-dict `:` type($base) `,` type($index) `->` type($address)";
  let hasVerifier = 1;
}

def RustTyped_SliceFromArrayOp : RustTyped_Op<"slice_from_array"> {
  let summary = "Coerce a typed Rust array pointer/reference to a slice fat pointer";
  let arguments = (ins AnyType:$source,
                       OptionalAttr<StrAttr>:$span);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$source attr-dict `:` type($source) `->` type($result)";
  let hasVerifier = 1;
}

def RustTyped_PtrMetadataOp : RustTyped_Op<"ptr_metadata"> {
  let summary = "Extract Rust pointer metadata";
  let arguments = (ins AnyType:$source,
                       OptionalAttr<StrAttr>:$span);
  let results = (outs AnyType:$metadata);
  let assemblyFormat = "$source attr-dict `:` type($source) `->` type($metadata)";
  let hasVerifier = 1;
}

def RustTyped_SubsliceOp : RustTyped_Op<"subslice"> {
  let summary = "Compute a typed Rust subslice fat pointer";
  let arguments = (ins AnyType:$source,
                       I64Attr:$from_index,
                       I64Attr:$to_index,
                       BoolAttr:$from_end,
                       OptionalAttr<StrAttr>:$span);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$source attr-dict `:` type($source) `->` type($result)";
  let hasVerifier = 1;
}

def RustTyped_SliceRangeOp : RustTyped_Op<"slice_range"> {
  let summary = "Compute a typed Rust slice fat pointer from dynamic range bounds";
  let arguments = (ins AnyType:$source,
                       AnyType:$start,
                       AnyType:$length,
                       OptionalAttr<StrAttr>:$span);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$source `[` $start `,` $length `]` attr-dict `:` type($source) `,` type($start) `,` type($length) `->` type($result)";
  let hasVerifier = 1;
}

def RustTyped_BorrowOp : RustTyped_Op<"borrow"> {
  let summary = "Create a typed Rust reference to a place address";
  let arguments = (ins AnyType:$slot,
                       RustBorrowKindAttr:$borrow_kind,
                       RustMutabilityAttr:$mutability,
                       OptionalAttr<StrAttr>:$rust_region,
                       OptionalAttr<StrAttr>:$span);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$slot attr-dict `:` type($slot) `->` type($result)";
  let hasVerifier = 1;
}

def RustTyped_RawAddressOp : RustTyped_Op<"address_of"> {
  let summary = "Create a typed Rust raw pointer to a place address";
  let arguments = (ins AnyType:$slot,
                       RustRawPtrKindAttr:$raw_ptr_kind,
                       RustMutabilityAttr:$mutability,
                       OptionalAttr<StrAttr>:$span);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$slot attr-dict `:` type($slot) `->` type($result)";
  let hasVerifier = 1;
}

def RustTyped_TypedGotoOp : RustTyped_Op<"goto", [Terminator]> {
  let summary = "Typed Rust MIR goto terminator placeholder";
  let arguments = (ins I64Attr:$target, OptionalAttr<StrAttr>:$span);
  let assemblyFormat = "attr-dict";
}

def RustTyped_TypedSwitchIntOp : RustTyped_Op<"switch_int", [Terminator]> {
  let summary = "Typed Rust MIR switch-int terminator placeholder";
  let arguments = (ins AnyType:$discr, OptionalAttr<DictionaryAttr>:$targets,
                       OptionalAttr<StrAttr>:$span);
  let assemblyFormat = "$discr attr-dict `:` type($discr)";
}

def RustTyped_TypedAssertOp : RustTyped_Op<"assert"> {
  let summary = "Typed Rust MIR assert";
  let arguments = (ins AnyType:$cond,
                       OptionalAttr<StrAttr>:$msg,
                       OptionalAttr<StrAttr>:$span);
  let assemblyFormat = "$cond attr-dict `:` type($cond)";
}

def RustTyped_TypedCallOp : RustTyped_Op<"call"> {
  let summary = "Typed Rust MIR direct call";
  let arguments = (ins FlatSymbolRefAttr:$callee,
                       Variadic<AnyType>:$args,
                       OptionalAttr<StrAttr>:$rust_name,
                       OptionalAttr<RustAbiAttr>:$abi,
                       OptionalAttr<StrAttr>:$callee_def,
                       OptionalAttr<StrAttr>:$callee_type,
                       OptionalAttr<StrAttr>:$callee_generic_args,
                       OptionalAttr<StrAttr>:$callee_inputs,
                       OptionalAttr<StrAttr>:$callee_output,
                       OptionalAttr<BoolAttr>:$c_variadic,
                       OptionalAttr<I64Attr>:$target,
                       OptionalAttr<RustUnwindActionAttr>:$unwind,
                       OptionalAttr<StrAttr>:$span);
  let results = (outs Variadic<AnyType>:$results);
  let hasVerifier = 1;
  let assemblyFormat = [{
    $callee `(` $args `)` attr-dict `:` functional-type($args, results)
  }];
}

def RustTyped_TypedCallIndirectOp : RustTyped_Op<"call_indirect"> {
  let summary = "Typed Rust MIR indirect call";
  let arguments = (ins AnyType:$callee,
                       Variadic<AnyType>:$args,
                       OptionalAttr<I64Attr>:$target,
                       OptionalAttr<RustUnwindActionAttr>:$unwind,
                       OptionalAttr<StrAttr>:$span);
  let results = (outs Variadic<AnyType>:$results);
  let assemblyFormat = [{
    $callee `(` $args `)` attr-dict `:` type($callee) `,` functional-type($args, results)
  }];
}

def RustTyped_FnAddrOp : RustTyped_Op<"fn_addr"> {
  let summary = "Typed Rust function address";
  let arguments = (ins FlatSymbolRefAttr:$callee,
                       OptionalAttr<StrAttr>:$rust_name,
                       OptionalAttr<StrAttr>:$span);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$callee attr-dict `:` type($result)";
}

def RustTyped_TypedReturnOp : RustTyped_Op<"return", [Terminator]> {
  let summary = "Typed Rust MIR return";
  let arguments = (ins Variadic<AnyType>:$values, OptionalAttr<StrAttr>:$span);
  let assemblyFormat = "attr-dict ($values^ `:` type($values))?";
}

def RustTyped_TypedUnreachableOp : RustTyped_Op<"unreachable", [Terminator]> {
  let summary = "Typed Rust MIR unreachable terminator placeholder";
  let arguments = (ins OptionalAttr<StrAttr>:$span);
  let assemblyFormat = "attr-dict";
}

#endif // MLIR_DIALECT_RUST_TYPED_IR_RUSTTYPEDOPS_TD
