//===- RustAttrs.td - Rust MIR dialect attributes -----------*- tablegen -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_DIALECT_RUSTMIR_IR_RUSTATTRS_TD
#define MLIR_DIALECT_RUSTMIR_IR_RUSTATTRS_TD

include "mlir/Dialect/RustMIR/IR/RustMIRDialect.td"
include "mlir/IR/EnumAttr.td"

class Rust_I32EnumAttr<string name, string summary,
                       list<I32EnumAttrCase> cases>
    : I32EnumAttr<name, summary, cases> {
  let cppNamespace = "::mlir::rust::mir";
  let genSpecializedAttr = 0;
}

class Rust_EnumAttr<EnumAttrInfo enumInfo, string name>
    : EnumAttr<RustMIR_Dialect, enumInfo, name> {
  let assemblyFormat = "`<` $value `>`";
}

def RustAbi : Rust_I32EnumAttr<"RustAbi", "Rust call ABI", [
  I32EnumAttrCase<"Rust", 0, "rust">,
  I32EnumAttrCase<"C", 1, "c">
]>;
def RustAbiAttr : Rust_EnumAttr<RustAbi, "abi">;

def RustUnwindAction : Rust_I32EnumAttr<"RustUnwindAction",
    "Rust MIR unwind action", [
  I32EnumAttrCase<"Continue", 0, "Continue">,
  I32EnumAttrCase<"Unreachable", 1, "Unreachable">,
  I32EnumAttrCase<"Terminate", 2, "Terminate">,
  I32EnumAttrCase<"Cleanup", 3, "Cleanup">
]>;
def RustUnwindActionAttr : Rust_EnumAttr<RustUnwindAction, "unwind">;

def RustBorrowKind : Rust_I32EnumAttr<"RustBorrowKind",
    "Rust MIR borrow kind", [
  I32EnumAttrCase<"Shared", 0, "Shared">,
  I32EnumAttrCase<"Fake", 1, "Fake">,
  I32EnumAttrCase<"Mut", 2, "Mut">
]>;
def RustBorrowKindAttr : Rust_EnumAttr<RustBorrowKind, "borrow_kind">;

def RustRawPtrKind : Rust_I32EnumAttr<"RustRawPtrKind",
    "Rust MIR raw pointer kind", [
  I32EnumAttrCase<"Mut", 0, "Mut">,
  I32EnumAttrCase<"Const", 1, "Const">,
  I32EnumAttrCase<"FakeForPtrMetadata", 2, "FakeForPtrMetadata">
]>;
def RustRawPtrKindAttr : Rust_EnumAttr<RustRawPtrKind, "raw_ptr_kind">;

def RustMutability : Rust_I32EnumAttr<"RustMutability",
    "Rust MIR mutability", [
  I32EnumAttrCase<"Mut", 0, "mut">,
  I32EnumAttrCase<"Shared", 1, "shared">,
  I32EnumAttrCase<"Const", 2, "const">
]>;
def RustMutabilityAttr : Rust_EnumAttr<RustMutability, "mutability">;

def RustLocalMutability : Rust_I32EnumAttr<"RustLocalMutability",
    "Rust MIR local mutability", [
  I32EnumAttrCase<"Mut", 0, "Mut">,
  I32EnumAttrCase<"Not", 1, "Not">
]>;
def RustLocalMutabilityAttr : Rust_EnumAttr<RustLocalMutability,
                                            "local_mutability">;

def RustAggregateKind : Rust_I32EnumAttr<"RustAggregateKind",
    "Rust MIR aggregate kind", [
  I32EnumAttrCase<"Array", 0, "Array">,
  I32EnumAttrCase<"Tuple", 1, "Tuple">,
  I32EnumAttrCase<"Adt", 2, "Adt">,
  I32EnumAttrCase<"Closure", 3, "Closure">,
  I32EnumAttrCase<"Coroutine", 4, "Coroutine">,
  I32EnumAttrCase<"CoroutineClosure", 5, "CoroutineClosure">,
  I32EnumAttrCase<"RawPtr", 6, "RawPtr">
]>;
def RustAggregateKindAttr : Rust_EnumAttr<RustAggregateKind, "aggregate_kind">;

def RustLocalRole : Rust_I32EnumAttr<"RustLocalRole",
    "Rust MIR local role", [
  I32EnumAttrCase<"Return", 0, "return">,
  I32EnumAttrCase<"Arg", 1, "arg">,
  I32EnumAttrCase<"Inner", 2, "inner">
]>;
def RustLocalRoleAttr : Rust_EnumAttr<RustLocalRole, "local_role">;

def RustBinaryOpKind : Rust_I32EnumAttr<"RustBinaryOpKind",
    "Rust MIR binary operator kind", [
  I32EnumAttrCase<"Add", 0, "Add">,
  I32EnumAttrCase<"AddUnchecked", 1, "AddUnchecked">,
  I32EnumAttrCase<"Sub", 2, "Sub">,
  I32EnumAttrCase<"SubUnchecked", 3, "SubUnchecked">,
  I32EnumAttrCase<"Mul", 4, "Mul">,
  I32EnumAttrCase<"MulUnchecked", 5, "MulUnchecked">,
  I32EnumAttrCase<"Div", 6, "Div">,
  I32EnumAttrCase<"Rem", 7, "Rem">,
  I32EnumAttrCase<"BitAnd", 8, "BitAnd">,
  I32EnumAttrCase<"BitOr", 9, "BitOr">,
  I32EnumAttrCase<"BitXor", 10, "BitXor">,
  I32EnumAttrCase<"Shl", 11, "Shl">,
  I32EnumAttrCase<"ShlUnchecked", 12, "ShlUnchecked">,
  I32EnumAttrCase<"Shr", 13, "Shr">,
  I32EnumAttrCase<"ShrUnchecked", 14, "ShrUnchecked">,
  I32EnumAttrCase<"Eq", 15, "Eq">,
  I32EnumAttrCase<"Ne", 16, "Ne">,
  I32EnumAttrCase<"Lt", 17, "Lt">,
  I32EnumAttrCase<"Le", 18, "Le">,
  I32EnumAttrCase<"Gt", 19, "Gt">,
  I32EnumAttrCase<"Ge", 20, "Ge">
]>;
def RustBinaryOpKindAttr : Rust_EnumAttr<RustBinaryOpKind, "binary_op">;

def RustUnaryOpKind : Rust_I32EnumAttr<"RustUnaryOpKind",
    "Rust MIR unary operator kind", [
  I32EnumAttrCase<"Neg", 0, "Neg">,
  I32EnumAttrCase<"Not", 1, "Not">,
  I32EnumAttrCase<"PtrMetadata", 2, "PtrMetadata">
]>;
def RustUnaryOpKindAttr : Rust_EnumAttr<RustUnaryOpKind, "unary_op">;

def RustCastKind : Rust_I32EnumAttr<"RustCastKind",
    "Rust MIR cast kind", [
  I32EnumAttrCase<"PointerExposeAddress", 0, "PointerExposeAddress">,
  I32EnumAttrCase<"PointerWithExposedProvenance", 1,
                  "PointerWithExposedProvenance">,
  I32EnumAttrCase<"PointerCoercion", 2, "PointerCoercion">,
  I32EnumAttrCase<"IntToInt", 3, "IntToInt">,
  I32EnumAttrCase<"FloatToInt", 4, "FloatToInt">,
  I32EnumAttrCase<"FloatToFloat", 5, "FloatToFloat">,
  I32EnumAttrCase<"IntToFloat", 6, "IntToFloat">,
  I32EnumAttrCase<"PtrToPtr", 7, "PtrToPtr">,
  I32EnumAttrCase<"FnPtrToPtr", 8, "FnPtrToPtr">,
  I32EnumAttrCase<"Transmute", 9, "Transmute">,
  I32EnumAttrCase<"Subtype", 10, "Subtype">
]>;
def RustCastKindAttr : Rust_EnumAttr<RustCastKind, "cast_kind">;

def RustRangeKind : Rust_I32EnumAttr<"RustRangeKind",
    "Rust range expression kind used to index a slice/array", [
  I32EnumAttrCase<"Full", 0, "Full">,
  I32EnumAttrCase<"FromTo", 1, "FromTo">,
  I32EnumAttrCase<"From", 2, "From">,
  I32EnumAttrCase<"To", 3, "To">,
  I32EnumAttrCase<"FromToInclusive", 4, "FromToInclusive">,
  I32EnumAttrCase<"ToInclusive", 5, "ToInclusive">
]>;
def RustRangeKindAttr : Rust_EnumAttr<RustRangeKind, "range_kind">;

#endif // MLIR_DIALECT_RUSTMIR_IR_RUSTATTRS_TD
