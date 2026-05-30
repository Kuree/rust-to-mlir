//===- RustOps.td - Rust MIR dialect operations ------------*- tablegen -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_DIALECT_RUSTMIR_IR_RUSTOPS_TD
#define MLIR_DIALECT_RUSTMIR_IR_RUSTOPS_TD

include "mlir/Dialect/RustMIR/IR/RustTypes.td"
include "mlir/IR/SymbolInterfaces.td"
include "mlir/Interfaces/SideEffectInterfaces.td"

class Rust_MirrorOp<string mnemonic, string opSummary> : Rust_Op<mnemonic> {
  let summary = opSummary;
  let description = [{
    This operation mirrors a rustc_public MIR enum variant that is not modeled
    with a dedicated standalone structure yet. Supported variants use direct
    attributes and nested MIR operations; fallback variants stay explicit so
    later passes fail closed instead of silently lowering incomplete semantics.
  }];
  let arguments = (ins OptionalAttr<StrAttr>:$mir_kind,
                       OptionalAttr<StrAttr>:$debug,
                       OptionalAttr<StrAttr>:$span);
  let assemblyFormat = "attr-dict";
}

class Rust_RegionOp<string mnemonic, string opSummary>
    : Rust_Op<mnemonic, [NoTerminator, SingleBlock]> {
  let summary = opSummary;
  let arguments = (ins OptionalAttr<StrAttr>:$span);
  let regions = (region SizedRegion<1>:$body);
  let assemblyFormat = "attr-dict-with-keyword $body";
}

class Rust_LeafOp<string mnemonic, string opSummary> : Rust_Op<mnemonic> {
  let summary = opSummary;
  let arguments = (ins OptionalAttr<StrAttr>:$span);
  let assemblyFormat = "attr-dict";
}

def Rust_FuncOp : Rust_Op<"func", [IsolatedFromAbove, NoTerminator, SingleBlock, Symbol]> {
  let summary = "Rust MIR function body";
  let arguments = (ins SymbolNameAttr:$sym_name,
                       OptionalAttr<StrAttr>:$rust_name,
                       OptionalAttr<StrAttr>:$signature,
                       OptionalAttr<I64Attr>:$arg_count,
                       OptionalAttr<StrAttr>:$span);
  let regions = (region SizedRegion<1>:$body);
  let assemblyFormat = "$sym_name attr-dict-with-keyword $body";
}

def Rust_LocalOp : Rust_Op<"local"> {
  let summary = "Rust MIR local declaration";
  let arguments = (ins I64Attr:$index,
                       OptionalAttr<StrAttr>:$name,
                       OptionalAttr<AnyAttr>:$rust_type,
                       OptionalAttr<StrAttr>:$mutability,
                       OptionalAttr<StrAttr>:$role,
                       OptionalAttr<StrAttr>:$span);
  let assemblyFormat = "attr-dict";
}

def Rust_BlockOp : Rust_Op<"block", [NoTerminator, SingleBlock]> {
  let summary = "Rust MIR basic block";
  let arguments = (ins I64Attr:$index, OptionalAttr<StrAttr>:$span);
  let regions = (region SizedRegion<1>:$body);
  let assemblyFormat = "$index attr-dict-with-keyword $body";
}

// StatementKind variants.
def Rust_AssignOp : Rust_Op<"assign", [NoTerminator, SingleBlock]> {
  let summary = "MIR StatementKind::Assign";
  let arguments = (ins I64Attr:$index,
                       OptionalAttr<StrAttr>:$mir_kind,
                       OptionalAttr<StrAttr>:$span);
  let regions = (region SizedRegion<1>:$body);
  let assemblyFormat = "$index attr-dict-with-keyword $body";
}
def Rust_FakeReadOp : Rust_MirrorOp<"fake_read", "MIR StatementKind::FakeRead">;
def Rust_SetDiscriminantOp : Rust_MirrorOp<"set_discriminant", "MIR StatementKind::SetDiscriminant">;
def Rust_DeinitOp : Rust_MirrorOp<"deinit", "MIR StatementKind::Deinit">;
def Rust_StorageLiveOp : Rust_MirrorOp<"storage_live", "MIR StatementKind::StorageLive">;
def Rust_StorageDeadOp : Rust_MirrorOp<"storage_dead", "MIR StatementKind::StorageDead">;
def Rust_RetagOp : Rust_MirrorOp<"retag", "MIR StatementKind::Retag">;
def Rust_PlaceMentionOp : Rust_MirrorOp<"place_mention", "MIR StatementKind::PlaceMention">;
def Rust_AscribeUserTypeOp : Rust_MirrorOp<"ascribe_user_type", "MIR StatementKind::AscribeUserType">;
def Rust_CoverageOp : Rust_MirrorOp<"coverage", "MIR StatementKind::Coverage">;
def Rust_IntrinsicOp : Rust_MirrorOp<"intrinsic", "MIR StatementKind::Intrinsic">;
def Rust_ConstEvalCounterOp : Rust_MirrorOp<"const_eval_counter", "MIR StatementKind::ConstEvalCounter">;
def Rust_NopOp : Rust_MirrorOp<"nop", "MIR StatementKind::Nop">;
def Rust_UnsupportedStatementOp : Rust_MirrorOp<"unsupported_statement", "Unsupported MIR statement">;

// Rvalue variants.
def Rust_AddressOfOp : Rust_Op<"address_of", [NoTerminator, SingleBlock]> {
  let summary = "MIR Rvalue::AddressOf";
  let arguments = (ins StrAttr:$raw_ptr_kind,
                       StrAttr:$mutability,
                       OptionalAttr<StrAttr>:$mir_kind,
                       OptionalAttr<StrAttr>:$debug,
                       OptionalAttr<StrAttr>:$span);
  let regions = (region SizedRegion<1>:$body);
  let assemblyFormat = "attr-dict-with-keyword $body";
}
def Rust_AggregateOp : Rust_Op<"aggregate", [NoTerminator, SingleBlock]> {
  let summary = "MIR Rvalue::Aggregate";
  let arguments = (ins StrAttr:$aggregate_kind,
                       OptionalAttr<StrAttr>:$mir_kind,
                       OptionalAttr<StrAttr>:$span);
  let regions = (region SizedRegion<1>:$body);
  let assemblyFormat = "attr-dict-with-keyword $body";
}
def Rust_BinaryOp : Rust_Op<"binary_op", [NoTerminator, SingleBlock]> {
  let summary = "MIR Rvalue::BinaryOp";
  let arguments = (ins StrAttr:$op,
                       OptionalAttr<StrAttr>:$mir_kind,
                       OptionalAttr<StrAttr>:$span);
  let regions = (region SizedRegion<1>:$body);
  let assemblyFormat = "attr-dict-with-keyword $body";
}
def Rust_CastOp : Rust_MirrorOp<"cast", "MIR Rvalue::Cast">;
def Rust_CheckedBinaryOp : Rust_Op<"checked_binary_op", [NoTerminator, SingleBlock]> {
  let summary = "MIR Rvalue::CheckedBinaryOp";
  let arguments = (ins StrAttr:$op,
                       OptionalAttr<StrAttr>:$mir_kind,
                       OptionalAttr<StrAttr>:$span);
  let regions = (region SizedRegion<1>:$body);
  let assemblyFormat = "attr-dict-with-keyword $body";
}
def Rust_CopyForDerefOp : Rust_MirrorOp<"copy_for_deref", "MIR Rvalue::CopyForDeref">;
def Rust_DiscriminantOp : Rust_MirrorOp<"discriminant", "MIR Rvalue::Discriminant">;
def Rust_LenOp : Rust_MirrorOp<"len", "MIR Rvalue::Len">;
def Rust_RefOp : Rust_Op<"ref", [NoTerminator, SingleBlock]> {
  let summary = "MIR Rvalue::Ref";
  let arguments = (ins StrAttr:$borrow_kind,
                       StrAttr:$mutability,
                       OptionalAttr<StrAttr>:$rust_region,
                       OptionalAttr<StrAttr>:$mir_kind,
                       OptionalAttr<StrAttr>:$debug,
                       OptionalAttr<StrAttr>:$span);
  let regions = (region SizedRegion<1>:$body);
  let assemblyFormat = "attr-dict-with-keyword $body";
}
def Rust_RepeatOp : Rust_MirrorOp<"repeat", "MIR Rvalue::Repeat">;
def Rust_ShallowInitBoxOp : Rust_MirrorOp<"shallow_init_box", "MIR Rvalue::ShallowInitBox">;
def Rust_ThreadLocalRefOp : Rust_MirrorOp<"thread_local_ref", "MIR Rvalue::ThreadLocalRef">;
def Rust_NullaryOp : Rust_MirrorOp<"nullary_op", "MIR Rvalue::NullaryOp">;
def Rust_UnaryOp : Rust_Op<"unary_op", [NoTerminator, SingleBlock]> {
  let summary = "MIR Rvalue::UnaryOp";
  let arguments = (ins StrAttr:$op,
                       OptionalAttr<StrAttr>:$mir_kind,
                       OptionalAttr<StrAttr>:$span);
  let regions = (region SizedRegion<1>:$body);
  let assemblyFormat = "attr-dict-with-keyword $body";
}
def Rust_UseOp : Rust_RegionOp<"use", "MIR Rvalue::Use">;

// TerminatorKind variants.
def Rust_GotoOp : Rust_Op<"goto"> {
  let summary = "MIR TerminatorKind::Goto";
  let arguments = (ins I64Attr:$target,
                       OptionalAttr<StrAttr>:$mir_kind,
                       OptionalAttr<StrAttr>:$debug,
                       OptionalAttr<StrAttr>:$span);
  let assemblyFormat = "attr-dict";
}
def Rust_SwitchIntOp : Rust_Op<"switch_int", [NoTerminator, SingleBlock]> {
  let summary = "MIR TerminatorKind::SwitchInt";
  let arguments = (ins OptionalAttr<DictionaryAttr>:$targets,
                       OptionalAttr<StrAttr>:$mir_kind,
                       OptionalAttr<StrAttr>:$debug,
                       OptionalAttr<StrAttr>:$span);
  let regions = (region SizedRegion<1>:$body);
  let assemblyFormat = "attr-dict-with-keyword $body";
}
def Rust_ResumeOp : Rust_MirrorOp<"resume", "MIR TerminatorKind::Resume">;
def Rust_AbortOp : Rust_MirrorOp<"abort", "MIR TerminatorKind::Abort">;
def Rust_ReturnOp : Rust_LeafOp<"return", "MIR TerminatorKind::Return">;
def Rust_UnreachableOp : Rust_MirrorOp<"unreachable", "MIR TerminatorKind::Unreachable">;
def Rust_DropOp : Rust_Op<"drop"> {
  let summary = "MIR TerminatorKind::Drop";
  let arguments = (ins I64Attr:$target,
                       OptionalAttr<StrAttr>:$mir_kind,
                       OptionalAttr<StrAttr>:$debug,
                       OptionalAttr<StrAttr>:$span);
  let assemblyFormat = "attr-dict";
}
def Rust_CallOp : Rust_Op<"call", [NoTerminator, SingleBlock]> {
  let summary = "MIR TerminatorKind::Call";
  let description = [{
    The body contains the callee operand, the destination place, then each call
    argument operand in MIR order.
  }];
  let arguments = (ins OptionalAttr<I64Attr>:$target,
                       OptionalAttr<StrAttr>:$mir_kind,
                       OptionalAttr<StrAttr>:$debug,
                       OptionalAttr<StrAttr>:$unwind,
                       OptionalAttr<StrAttr>:$callee_name,
                       OptionalAttr<StrAttr>:$callee_def,
                       OptionalAttr<StrAttr>:$callee_type,
                       OptionalAttr<StrAttr>:$callee_generic_args,
                       OptionalAttr<StrAttr>:$callee_inputs,
                       OptionalAttr<StrAttr>:$callee_output,
                       OptionalAttr<StrAttr>:$callee_abi,
                       OptionalAttr<BoolAttr>:$callee_c_variadic,
                       OptionalAttr<StrAttr>:$span);
  let regions = (region SizedRegion<1>:$body);
  let hasVerifier = 1;
  let assemblyFormat = "attr-dict-with-keyword $body";
}
def Rust_AssertOp : Rust_Op<"assert", [NoTerminator, SingleBlock]> {
  let summary = "MIR TerminatorKind::Assert";
  let arguments = (ins BoolAttr:$expected,
                       I64Attr:$target,
                       OptionalAttr<StrAttr>:$mir_kind,
                       OptionalAttr<StrAttr>:$debug,
                       OptionalAttr<StrAttr>:$span);
  let regions = (region SizedRegion<1>:$body);
  let assemblyFormat = "attr-dict-with-keyword $body";
}
def Rust_InlineAsmOp : Rust_Op<"inline_asm"> {
  let summary = "MIR TerminatorKind::InlineAsm";
  let arguments = (ins I64Attr:$target,
                       OptionalAttr<StrAttr>:$mir_kind,
                       OptionalAttr<StrAttr>:$debug,
                       OptionalAttr<StrAttr>:$span);
  let assemblyFormat = "attr-dict";
}
def Rust_UnsupportedTerminatorOp : Rust_MirrorOp<"unsupported_terminator", "Unsupported MIR terminator">;

// MIR leaf structures.
def Rust_PlaceOp : Rust_Op<"place", [NoTerminator, SingleBlock]> {
  let summary = "MIR Place";
  let arguments = (ins I64Attr:$local,
                       OptionalAttr<StrAttr>:$span);
  let regions = (region SizedRegion<1>:$body);
  let assemblyFormat = "$local attr-dict-with-keyword $body";
}
def Rust_ProjectionOp : Rust_MirrorOp<"projection", "MIR ProjectionElem">;
def Rust_CopyOp : Rust_RegionOp<"copy", "MIR Operand::Copy">;
def Rust_MoveOp : Rust_RegionOp<"move", "MIR Operand::Move">;
def Rust_ConstOp : Rust_MirrorOp<"const", "MIR Operand::Const">;
def Rust_ConstantOp : Rust_Op<"constant"> {
  let summary = "MIR Operand::Constant";
  let arguments = (ins OptionalAttr<AnyAttr>:$value,
                       OptionalAttr<StrAttr>:$debug,
                       OptionalAttr<StrAttr>:$ty,
                       OptionalAttr<StrAttr>:$mir_kind,
                       OptionalAttr<StrAttr>:$span);
  let assemblyFormat = "attr-dict";
}
def Rust_RuntimeChecksOp : Rust_MirrorOp<"runtime_checks", "MIR Operand::RuntimeChecks">;

// AggregateKind variants.
def Rust_AggregateArrayOp : Rust_MirrorOp<"aggregate_array", "MIR AggregateKind::Array">;
def Rust_AggregateTupleOp : Rust_MirrorOp<"aggregate_tuple", "MIR AggregateKind::Tuple">;
def Rust_AggregateAdtOp : Rust_MirrorOp<"aggregate_adt", "MIR AggregateKind::Adt">;
def Rust_AggregateClosureOp : Rust_MirrorOp<"aggregate_closure", "MIR AggregateKind::Closure">;
def Rust_AggregateCoroutineOp : Rust_MirrorOp<"aggregate_coroutine", "MIR AggregateKind::Coroutine">;
def Rust_AggregateCoroutineClosureOp : Rust_MirrorOp<"aggregate_coroutine_closure", "MIR AggregateKind::CoroutineClosure">;
def Rust_AggregateRawPtrOp : Rust_MirrorOp<"aggregate_raw_ptr", "MIR AggregateKind::RawPtr">;

// ProjectionElem variants.
def Rust_ProjectionDerefOp : Rust_Op<"projection_deref"> {
  let summary = "MIR ProjectionElem::Deref";
  let arguments = (ins OptionalAttr<StrAttr>:$mir_kind,
                       OptionalAttr<StrAttr>:$span);
  let assemblyFormat = "attr-dict";
}
def Rust_ProjectionFieldOp : Rust_Op<"projection_field"> {
  let summary = "MIR ProjectionElem::Field";
  let arguments = (ins I64Attr:$index,
                       OptionalAttr<StrAttr>:$ty,
                       OptionalAttr<StrAttr>:$mir_kind,
                       OptionalAttr<StrAttr>:$span);
  let assemblyFormat = "attr-dict";
}
def Rust_ProjectionIndexOp : Rust_Op<"projection_index"> {
  let summary = "MIR ProjectionElem::Index";
  let arguments = (ins I64Attr:$local,
                       OptionalAttr<StrAttr>:$mir_kind,
                       OptionalAttr<StrAttr>:$span);
  let assemblyFormat = "attr-dict";
}
def Rust_ProjectionConstantIndexOp : Rust_Op<"projection_constant_index"> {
  let summary = "MIR ProjectionElem::ConstantIndex";
  let arguments = (ins I64Attr:$offset,
                       I64Attr:$min_length,
                       BoolAttr:$from_end,
                       OptionalAttr<StrAttr>:$mir_kind,
                       OptionalAttr<StrAttr>:$span);
  let assemblyFormat = "attr-dict";
}
def Rust_ProjectionSubsliceOp : Rust_Op<"projection_subslice"> {
  let summary = "MIR ProjectionElem::Subslice";
  let arguments = (ins I64Attr:$from_index,
                       I64Attr:$to_index,
                       BoolAttr:$from_end,
                       OptionalAttr<StrAttr>:$mir_kind,
                       OptionalAttr<StrAttr>:$span);
  let assemblyFormat = "attr-dict";
}
def Rust_ProjectionDowncastOp : Rust_MirrorOp<"projection_downcast", "MIR ProjectionElem::Downcast">;
def Rust_ProjectionOpaqueCastOp : Rust_MirrorOp<"projection_opaque_cast", "MIR ProjectionElem::OpaqueCast">;

// BinOp variants.
def Rust_BinOpAddOp : Rust_MirrorOp<"binop_add", "MIR BinOp::Add">;
def Rust_BinOpAddUncheckedOp : Rust_MirrorOp<"binop_add_unchecked", "MIR BinOp::AddUnchecked">;
def Rust_BinOpSubOp : Rust_MirrorOp<"binop_sub", "MIR BinOp::Sub">;
def Rust_BinOpSubUncheckedOp : Rust_MirrorOp<"binop_sub_unchecked", "MIR BinOp::SubUnchecked">;
def Rust_BinOpMulOp : Rust_MirrorOp<"binop_mul", "MIR BinOp::Mul">;
def Rust_BinOpMulUncheckedOp : Rust_MirrorOp<"binop_mul_unchecked", "MIR BinOp::MulUnchecked">;
def Rust_BinOpDivOp : Rust_MirrorOp<"binop_div", "MIR BinOp::Div">;
def Rust_BinOpRemOp : Rust_MirrorOp<"binop_rem", "MIR BinOp::Rem">;
def Rust_BinOpBitXorOp : Rust_MirrorOp<"binop_bit_xor", "MIR BinOp::BitXor">;
def Rust_BinOpBitAndOp : Rust_MirrorOp<"binop_bit_and", "MIR BinOp::BitAnd">;
def Rust_BinOpBitOrOp : Rust_MirrorOp<"binop_bit_or", "MIR BinOp::BitOr">;
def Rust_BinOpShlOp : Rust_MirrorOp<"binop_shl", "MIR BinOp::Shl">;
def Rust_BinOpShlUncheckedOp : Rust_MirrorOp<"binop_shl_unchecked", "MIR BinOp::ShlUnchecked">;
def Rust_BinOpShrOp : Rust_MirrorOp<"binop_shr", "MIR BinOp::Shr">;
def Rust_BinOpShrUncheckedOp : Rust_MirrorOp<"binop_shr_unchecked", "MIR BinOp::ShrUnchecked">;
def Rust_BinOpEqOp : Rust_MirrorOp<"binop_eq", "MIR BinOp::Eq">;
def Rust_BinOpLtOp : Rust_MirrorOp<"binop_lt", "MIR BinOp::Lt">;
def Rust_BinOpLeOp : Rust_MirrorOp<"binop_le", "MIR BinOp::Le">;
def Rust_BinOpNeOp : Rust_MirrorOp<"binop_ne", "MIR BinOp::Ne">;
def Rust_BinOpGeOp : Rust_MirrorOp<"binop_ge", "MIR BinOp::Ge">;
def Rust_BinOpGtOp : Rust_MirrorOp<"binop_gt", "MIR BinOp::Gt">;
def Rust_BinOpCmpOp : Rust_MirrorOp<"binop_cmp", "MIR BinOp::Cmp">;
def Rust_BinOpOffsetOp : Rust_MirrorOp<"binop_offset", "MIR BinOp::Offset">;

// UnOp variants.
def Rust_UnOpNotOp : Rust_MirrorOp<"unop_not", "MIR UnOp::Not">;
def Rust_UnOpNegOp : Rust_MirrorOp<"unop_neg", "MIR UnOp::Neg">;
def Rust_UnOpPtrMetadataOp : Rust_MirrorOp<"unop_ptr_metadata", "MIR UnOp::PtrMetadata">;

// CastKind variants.
def Rust_CastPointerExposeAddressOp : Rust_MirrorOp<"cast_pointer_expose_address", "MIR CastKind::PointerExposeAddress">;
def Rust_CastPointerWithExposedProvenanceOp : Rust_MirrorOp<"cast_pointer_with_exposed_provenance", "MIR CastKind::PointerWithExposedProvenance">;
def Rust_CastPointerCoercionOp : Rust_MirrorOp<"cast_pointer_coercion", "MIR CastKind::PointerCoercion">;
def Rust_CastIntToIntOp : Rust_MirrorOp<"cast_int_to_int", "MIR CastKind::IntToInt">;
def Rust_CastFloatToIntOp : Rust_MirrorOp<"cast_float_to_int", "MIR CastKind::FloatToInt">;
def Rust_CastFloatToFloatOp : Rust_MirrorOp<"cast_float_to_float", "MIR CastKind::FloatToFloat">;
def Rust_CastIntToFloatOp : Rust_MirrorOp<"cast_int_to_float", "MIR CastKind::IntToFloat">;
def Rust_CastPtrToPtrOp : Rust_MirrorOp<"cast_ptr_to_ptr", "MIR CastKind::PtrToPtr">;
def Rust_CastFnPtrToPtrOp : Rust_MirrorOp<"cast_fn_ptr_to_ptr", "MIR CastKind::FnPtrToPtr">;
def Rust_CastTransmuteOp : Rust_MirrorOp<"cast_transmute", "MIR CastKind::Transmute">;
def Rust_CastSubtypeOp : Rust_MirrorOp<"cast_subtype", "MIR CastKind::Subtype">;

// Borrow and pointer kind variants.
def Rust_BorrowSharedOp : Rust_MirrorOp<"borrow_shared", "MIR BorrowKind::Shared">;
def Rust_BorrowFakeOp : Rust_MirrorOp<"borrow_fake", "MIR BorrowKind::Fake">;
def Rust_BorrowMutOp : Rust_MirrorOp<"borrow_mut", "MIR BorrowKind::Mut">;
def Rust_RawPtrMutOp : Rust_MirrorOp<"raw_ptr_mut", "MIR RawPtrKind::Mut">;
def Rust_RawPtrConstOp : Rust_MirrorOp<"raw_ptr_const", "MIR RawPtrKind::Const">;
def Rust_RawPtrFakeForPtrMetadataOp : Rust_MirrorOp<"raw_ptr_fake_for_ptr_metadata", "MIR RawPtrKind::FakeForPtrMetadata">;
def Rust_MutBorrowDefaultOp : Rust_MirrorOp<"mut_borrow_default", "MIR MutBorrowKind::Default">;
def Rust_MutBorrowTwoPhaseOp : Rust_MirrorOp<"mut_borrow_two_phase", "MIR MutBorrowKind::TwoPhaseBorrow">;
def Rust_MutBorrowClosureCaptureOp : Rust_MirrorOp<"mut_borrow_closure_capture", "MIR MutBorrowKind::ClosureCapture">;
def Rust_FakeBorrowDeepOp : Rust_MirrorOp<"fake_borrow_deep", "MIR FakeBorrowKind::Deep">;
def Rust_FakeBorrowShallowOp : Rust_MirrorOp<"fake_borrow_shallow", "MIR FakeBorrowKind::Shallow">;

// Statement enum variants.
def Rust_FakeReadForMatchGuardOp : Rust_MirrorOp<"fake_read_for_match_guard", "MIR FakeReadCause::ForMatchGuard">;
def Rust_FakeReadForMatchedPlaceOp : Rust_MirrorOp<"fake_read_for_matched_place", "MIR FakeReadCause::ForMatchedPlace">;
def Rust_FakeReadForGuardBindingOp : Rust_MirrorOp<"fake_read_for_guard_binding", "MIR FakeReadCause::ForGuardBinding">;
def Rust_FakeReadForLetOp : Rust_MirrorOp<"fake_read_for_let", "MIR FakeReadCause::ForLet">;
def Rust_FakeReadForIndexOp : Rust_MirrorOp<"fake_read_for_index", "MIR FakeReadCause::ForIndex">;
def Rust_RetagFnEntryOp : Rust_MirrorOp<"retag_fn_entry", "MIR RetagKind::FnEntry">;
def Rust_RetagTwoPhaseOp : Rust_MirrorOp<"retag_two_phase", "MIR RetagKind::TwoPhase">;
def Rust_RetagRawOp : Rust_MirrorOp<"retag_raw", "MIR RetagKind::Raw">;
def Rust_RetagDefaultOp : Rust_MirrorOp<"retag_default", "MIR RetagKind::Default">;
def Rust_VarianceCovariantOp : Rust_MirrorOp<"variance_covariant", "MIR Variance::Covariant">;
def Rust_VarianceInvariantOp : Rust_MirrorOp<"variance_invariant", "MIR Variance::Invariant">;
def Rust_VarianceContravariantOp : Rust_MirrorOp<"variance_contravariant", "MIR Variance::Contravariant">;
def Rust_VarianceBivariantOp : Rust_MirrorOp<"variance_bivariant", "MIR Variance::Bivariant">;
def Rust_IntrinsicAssumeOp : Rust_MirrorOp<"intrinsic_assume", "MIR NonDivergingIntrinsic::Assume">;
def Rust_IntrinsicCopyNonOverlappingOp : Rust_MirrorOp<"intrinsic_copy_nonoverlapping", "MIR NonDivergingIntrinsic::CopyNonOverlapping">;

// RuntimeChecks variants.
def Rust_RuntimeChecksUbOp : Rust_MirrorOp<"runtime_checks_ub", "MIR RuntimeChecks::UbChecks">;
def Rust_RuntimeChecksContractOp : Rust_MirrorOp<"runtime_checks_contract", "MIR RuntimeChecks::ContractChecks">;
def Rust_RuntimeChecksOverflowOp : Rust_MirrorOp<"runtime_checks_overflow", "MIR RuntimeChecks::OverflowChecks">;

// UnwindAction and AssertMessage variants.
def Rust_UnwindContinueOp : Rust_MirrorOp<"unwind_continue", "MIR UnwindAction::Continue">;
def Rust_UnwindUnreachableOp : Rust_MirrorOp<"unwind_unreachable", "MIR UnwindAction::Unreachable">;
def Rust_UnwindTerminateOp : Rust_MirrorOp<"unwind_terminate", "MIR UnwindAction::Terminate">;
def Rust_UnwindCleanupOp : Rust_MirrorOp<"unwind_cleanup", "MIR UnwindAction::Cleanup">;
def Rust_AssertBoundsCheckOp : Rust_MirrorOp<"assert_bounds_check", "MIR AssertMessage::BoundsCheck">;
def Rust_AssertOverflowOp : Rust_MirrorOp<"assert_overflow", "MIR AssertMessage::Overflow">;
def Rust_AssertOverflowNegOp : Rust_MirrorOp<"assert_overflow_neg", "MIR AssertMessage::OverflowNeg">;
def Rust_AssertDivisionByZeroOp : Rust_MirrorOp<"assert_division_by_zero", "MIR AssertMessage::DivisionByZero">;
def Rust_AssertRemainderByZeroOp : Rust_MirrorOp<"assert_remainder_by_zero", "MIR AssertMessage::RemainderByZero">;
def Rust_AssertResumedAfterReturnOp : Rust_MirrorOp<"assert_resumed_after_return", "MIR AssertMessage::ResumedAfterReturn">;
def Rust_AssertResumedAfterPanicOp : Rust_MirrorOp<"assert_resumed_after_panic", "MIR AssertMessage::ResumedAfterPanic">;
def Rust_AssertResumedAfterDropOp : Rust_MirrorOp<"assert_resumed_after_drop", "MIR AssertMessage::ResumedAfterDrop">;
def Rust_AssertMisalignedPointerDereferenceOp : Rust_MirrorOp<"assert_misaligned_pointer_dereference", "MIR AssertMessage::MisalignedPointerDereference">;
def Rust_AssertNullPointerDereferenceOp : Rust_MirrorOp<"assert_null_pointer_dereference", "MIR AssertMessage::NullPointerDereference">;
def Rust_AssertInvalidEnumConstructionOp : Rust_MirrorOp<"assert_invalid_enum_construction", "MIR AssertMessage::InvalidEnumConstruction">;

// Other MIR enum variants.
def Rust_MutabilityNotOp : Rust_MirrorOp<"mutability_not", "MIR Mutability::Not">;
def Rust_MutabilityMutOp : Rust_MirrorOp<"mutability_mut", "MIR Mutability::Mut">;
def Rust_SafetySafeOp : Rust_MirrorOp<"safety_safe", "MIR Safety::Safe">;
def Rust_SafetyUnsafeOp : Rust_MirrorOp<"safety_unsafe", "MIR Safety::Unsafe">;
def Rust_PointerCoercionReifyFnPointerOp : Rust_MirrorOp<"pointer_coercion_reify_fn_pointer", "MIR PointerCoercion::ReifyFnPointer">;
def Rust_PointerCoercionUnsafeFnPointerOp : Rust_MirrorOp<"pointer_coercion_unsafe_fn_pointer", "MIR PointerCoercion::UnsafeFnPointer">;
def Rust_PointerCoercionClosureFnPointerOp : Rust_MirrorOp<"pointer_coercion_closure_fn_pointer", "MIR PointerCoercion::ClosureFnPointer">;
def Rust_PointerCoercionMutToConstPointerOp : Rust_MirrorOp<"pointer_coercion_mut_to_const_pointer", "MIR PointerCoercion::MutToConstPointer">;
def Rust_PointerCoercionArrayToPointerOp : Rust_MirrorOp<"pointer_coercion_array_to_pointer", "MIR PointerCoercion::ArrayToPointer">;
def Rust_PointerCoercionUnsizeOp : Rust_MirrorOp<"pointer_coercion_unsize", "MIR PointerCoercion::Unsize">;
def Rust_CoroutineKindDesugaredOp : Rust_MirrorOp<"coroutine_kind_desugared", "MIR CoroutineKind::Desugared">;
def Rust_CoroutineKindCoroutineOp : Rust_MirrorOp<"coroutine_kind_coroutine", "MIR CoroutineKind::Coroutine">;
def Rust_CoroutineSourceBlockOp : Rust_MirrorOp<"coroutine_source_block", "MIR CoroutineSource::Block">;
def Rust_CoroutineSourceClosureOp : Rust_MirrorOp<"coroutine_source_closure", "MIR CoroutineSource::Closure">;
def Rust_CoroutineSourceFnOp : Rust_MirrorOp<"coroutine_source_fn", "MIR CoroutineSource::Fn">;
def Rust_CoroutineDesugaringAsyncOp : Rust_MirrorOp<"coroutine_desugaring_async", "MIR CoroutineDesugaring::Async">;
def Rust_CoroutineDesugaringGenOp : Rust_MirrorOp<"coroutine_desugaring_gen", "MIR CoroutineDesugaring::Gen">;
def Rust_CoroutineDesugaringAsyncGenOp : Rust_MirrorOp<"coroutine_desugaring_async_gen", "MIR CoroutineDesugaring::AsyncGen">;
def Rust_MovabilityStaticOp : Rust_MirrorOp<"movability_static", "MIR Coroutine Movability::Static">;
def Rust_MovabilityMovableOp : Rust_MirrorOp<"movability_movable", "MIR Coroutine Movability::Movable">;

#endif // MLIR_DIALECT_RUSTMIR_IR_RUSTOPS_TD
