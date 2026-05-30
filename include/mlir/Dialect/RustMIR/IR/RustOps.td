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

class Rust_PayloadOp<string mnemonic, string opSummary> : Rust_Op<mnemonic> {
  let summary = opSummary;
  let description = [{
    This operation mirrors a rustc_public MIR enum variant. The `payload`
    dictionary preserves the original variant fields in the NDJSON bridge
    format so that import remains lossless for the current frontend.
  }];
  let arguments = (ins OptionalAttr<DictionaryAttr>:$payload,
                       OptionalAttr<StrAttr>:$span);
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
def Rust_AssignOp : Rust_PayloadOp<"assign", "MIR StatementKind::Assign">;
def Rust_FakeReadOp : Rust_PayloadOp<"fake_read", "MIR StatementKind::FakeRead">;
def Rust_SetDiscriminantOp : Rust_PayloadOp<"set_discriminant", "MIR StatementKind::SetDiscriminant">;
def Rust_DeinitOp : Rust_PayloadOp<"deinit", "MIR StatementKind::Deinit">;
def Rust_StorageLiveOp : Rust_PayloadOp<"storage_live", "MIR StatementKind::StorageLive">;
def Rust_StorageDeadOp : Rust_PayloadOp<"storage_dead", "MIR StatementKind::StorageDead">;
def Rust_RetagOp : Rust_PayloadOp<"retag", "MIR StatementKind::Retag">;
def Rust_PlaceMentionOp : Rust_PayloadOp<"place_mention", "MIR StatementKind::PlaceMention">;
def Rust_AscribeUserTypeOp : Rust_PayloadOp<"ascribe_user_type", "MIR StatementKind::AscribeUserType">;
def Rust_CoverageOp : Rust_PayloadOp<"coverage", "MIR StatementKind::Coverage">;
def Rust_IntrinsicOp : Rust_PayloadOp<"intrinsic", "MIR StatementKind::Intrinsic">;
def Rust_ConstEvalCounterOp : Rust_PayloadOp<"const_eval_counter", "MIR StatementKind::ConstEvalCounter">;
def Rust_NopOp : Rust_PayloadOp<"nop", "MIR StatementKind::Nop">;

// Rvalue variants.
def Rust_AddressOfOp : Rust_PayloadOp<"address_of", "MIR Rvalue::AddressOf">;
def Rust_AggregateOp : Rust_PayloadOp<"aggregate", "MIR Rvalue::Aggregate">;
def Rust_BinaryOp : Rust_PayloadOp<"binary_op", "MIR Rvalue::BinaryOp">;
def Rust_CastOp : Rust_PayloadOp<"cast", "MIR Rvalue::Cast">;
def Rust_CheckedBinaryOp : Rust_PayloadOp<"checked_binary_op", "MIR Rvalue::CheckedBinaryOp">;
def Rust_CopyForDerefOp : Rust_PayloadOp<"copy_for_deref", "MIR Rvalue::CopyForDeref">;
def Rust_DiscriminantOp : Rust_PayloadOp<"discriminant", "MIR Rvalue::Discriminant">;
def Rust_LenOp : Rust_PayloadOp<"len", "MIR Rvalue::Len">;
def Rust_RefOp : Rust_PayloadOp<"ref", "MIR Rvalue::Ref">;
def Rust_RepeatOp : Rust_PayloadOp<"repeat", "MIR Rvalue::Repeat">;
def Rust_ShallowInitBoxOp : Rust_PayloadOp<"shallow_init_box", "MIR Rvalue::ShallowInitBox">;
def Rust_ThreadLocalRefOp : Rust_PayloadOp<"thread_local_ref", "MIR Rvalue::ThreadLocalRef">;
def Rust_NullaryOp : Rust_PayloadOp<"nullary_op", "MIR Rvalue::NullaryOp">;
def Rust_UnaryOp : Rust_PayloadOp<"unary_op", "MIR Rvalue::UnaryOp">;
def Rust_UseOp : Rust_PayloadOp<"use", "MIR Rvalue::Use">;

// TerminatorKind variants.
def Rust_GotoOp : Rust_PayloadOp<"goto", "MIR TerminatorKind::Goto">;
def Rust_SwitchIntOp : Rust_PayloadOp<"switch_int", "MIR TerminatorKind::SwitchInt">;
def Rust_ResumeOp : Rust_PayloadOp<"resume", "MIR TerminatorKind::Resume">;
def Rust_AbortOp : Rust_PayloadOp<"abort", "MIR TerminatorKind::Abort">;
def Rust_ReturnOp : Rust_PayloadOp<"return", "MIR TerminatorKind::Return">;
def Rust_UnreachableOp : Rust_PayloadOp<"unreachable", "MIR TerminatorKind::Unreachable">;
def Rust_DropOp : Rust_PayloadOp<"drop", "MIR TerminatorKind::Drop">;
def Rust_CallOp : Rust_PayloadOp<"call", "MIR TerminatorKind::Call">;
def Rust_AssertOp : Rust_PayloadOp<"assert", "MIR TerminatorKind::Assert">;
def Rust_InlineAsmOp : Rust_PayloadOp<"inline_asm", "MIR TerminatorKind::InlineAsm">;

// MIR leaf structures commonly referenced by payloads.
def Rust_PlaceOp : Rust_PayloadOp<"place", "MIR Place">;
def Rust_ProjectionOp : Rust_PayloadOp<"projection", "MIR ProjectionElem">;
def Rust_CopyOp : Rust_PayloadOp<"copy", "MIR Operand::Copy">;
def Rust_MoveOp : Rust_PayloadOp<"move", "MIR Operand::Move">;
def Rust_ConstOp : Rust_PayloadOp<"const", "MIR Operand::Const">;
def Rust_ConstantOp : Rust_PayloadOp<"constant", "MIR Operand::Constant">;
def Rust_RuntimeChecksOp : Rust_PayloadOp<"runtime_checks", "MIR Operand::RuntimeChecks">;

// AggregateKind variants.
def Rust_AggregateArrayOp : Rust_PayloadOp<"aggregate_array", "MIR AggregateKind::Array">;
def Rust_AggregateTupleOp : Rust_PayloadOp<"aggregate_tuple", "MIR AggregateKind::Tuple">;
def Rust_AggregateAdtOp : Rust_PayloadOp<"aggregate_adt", "MIR AggregateKind::Adt">;
def Rust_AggregateClosureOp : Rust_PayloadOp<"aggregate_closure", "MIR AggregateKind::Closure">;
def Rust_AggregateCoroutineOp : Rust_PayloadOp<"aggregate_coroutine", "MIR AggregateKind::Coroutine">;
def Rust_AggregateCoroutineClosureOp : Rust_PayloadOp<"aggregate_coroutine_closure", "MIR AggregateKind::CoroutineClosure">;
def Rust_AggregateRawPtrOp : Rust_PayloadOp<"aggregate_raw_ptr", "MIR AggregateKind::RawPtr">;

// ProjectionElem variants.
def Rust_ProjectionDerefOp : Rust_PayloadOp<"projection_deref", "MIR ProjectionElem::Deref">;
def Rust_ProjectionFieldOp : Rust_PayloadOp<"projection_field", "MIR ProjectionElem::Field">;
def Rust_ProjectionIndexOp : Rust_PayloadOp<"projection_index", "MIR ProjectionElem::Index">;
def Rust_ProjectionConstantIndexOp : Rust_PayloadOp<"projection_constant_index", "MIR ProjectionElem::ConstantIndex">;
def Rust_ProjectionSubsliceOp : Rust_PayloadOp<"projection_subslice", "MIR ProjectionElem::Subslice">;
def Rust_ProjectionDowncastOp : Rust_PayloadOp<"projection_downcast", "MIR ProjectionElem::Downcast">;
def Rust_ProjectionOpaqueCastOp : Rust_PayloadOp<"projection_opaque_cast", "MIR ProjectionElem::OpaqueCast">;

// BinOp variants.
def Rust_BinOpAddOp : Rust_PayloadOp<"binop_add", "MIR BinOp::Add">;
def Rust_BinOpAddUncheckedOp : Rust_PayloadOp<"binop_add_unchecked", "MIR BinOp::AddUnchecked">;
def Rust_BinOpSubOp : Rust_PayloadOp<"binop_sub", "MIR BinOp::Sub">;
def Rust_BinOpSubUncheckedOp : Rust_PayloadOp<"binop_sub_unchecked", "MIR BinOp::SubUnchecked">;
def Rust_BinOpMulOp : Rust_PayloadOp<"binop_mul", "MIR BinOp::Mul">;
def Rust_BinOpMulUncheckedOp : Rust_PayloadOp<"binop_mul_unchecked", "MIR BinOp::MulUnchecked">;
def Rust_BinOpDivOp : Rust_PayloadOp<"binop_div", "MIR BinOp::Div">;
def Rust_BinOpRemOp : Rust_PayloadOp<"binop_rem", "MIR BinOp::Rem">;
def Rust_BinOpBitXorOp : Rust_PayloadOp<"binop_bit_xor", "MIR BinOp::BitXor">;
def Rust_BinOpBitAndOp : Rust_PayloadOp<"binop_bit_and", "MIR BinOp::BitAnd">;
def Rust_BinOpBitOrOp : Rust_PayloadOp<"binop_bit_or", "MIR BinOp::BitOr">;
def Rust_BinOpShlOp : Rust_PayloadOp<"binop_shl", "MIR BinOp::Shl">;
def Rust_BinOpShlUncheckedOp : Rust_PayloadOp<"binop_shl_unchecked", "MIR BinOp::ShlUnchecked">;
def Rust_BinOpShrOp : Rust_PayloadOp<"binop_shr", "MIR BinOp::Shr">;
def Rust_BinOpShrUncheckedOp : Rust_PayloadOp<"binop_shr_unchecked", "MIR BinOp::ShrUnchecked">;
def Rust_BinOpEqOp : Rust_PayloadOp<"binop_eq", "MIR BinOp::Eq">;
def Rust_BinOpLtOp : Rust_PayloadOp<"binop_lt", "MIR BinOp::Lt">;
def Rust_BinOpLeOp : Rust_PayloadOp<"binop_le", "MIR BinOp::Le">;
def Rust_BinOpNeOp : Rust_PayloadOp<"binop_ne", "MIR BinOp::Ne">;
def Rust_BinOpGeOp : Rust_PayloadOp<"binop_ge", "MIR BinOp::Ge">;
def Rust_BinOpGtOp : Rust_PayloadOp<"binop_gt", "MIR BinOp::Gt">;
def Rust_BinOpCmpOp : Rust_PayloadOp<"binop_cmp", "MIR BinOp::Cmp">;
def Rust_BinOpOffsetOp : Rust_PayloadOp<"binop_offset", "MIR BinOp::Offset">;

// UnOp variants.
def Rust_UnOpNotOp : Rust_PayloadOp<"unop_not", "MIR UnOp::Not">;
def Rust_UnOpNegOp : Rust_PayloadOp<"unop_neg", "MIR UnOp::Neg">;
def Rust_UnOpPtrMetadataOp : Rust_PayloadOp<"unop_ptr_metadata", "MIR UnOp::PtrMetadata">;

// CastKind variants.
def Rust_CastPointerExposeAddressOp : Rust_PayloadOp<"cast_pointer_expose_address", "MIR CastKind::PointerExposeAddress">;
def Rust_CastPointerWithExposedProvenanceOp : Rust_PayloadOp<"cast_pointer_with_exposed_provenance", "MIR CastKind::PointerWithExposedProvenance">;
def Rust_CastPointerCoercionOp : Rust_PayloadOp<"cast_pointer_coercion", "MIR CastKind::PointerCoercion">;
def Rust_CastIntToIntOp : Rust_PayloadOp<"cast_int_to_int", "MIR CastKind::IntToInt">;
def Rust_CastFloatToIntOp : Rust_PayloadOp<"cast_float_to_int", "MIR CastKind::FloatToInt">;
def Rust_CastFloatToFloatOp : Rust_PayloadOp<"cast_float_to_float", "MIR CastKind::FloatToFloat">;
def Rust_CastIntToFloatOp : Rust_PayloadOp<"cast_int_to_float", "MIR CastKind::IntToFloat">;
def Rust_CastPtrToPtrOp : Rust_PayloadOp<"cast_ptr_to_ptr", "MIR CastKind::PtrToPtr">;
def Rust_CastFnPtrToPtrOp : Rust_PayloadOp<"cast_fn_ptr_to_ptr", "MIR CastKind::FnPtrToPtr">;
def Rust_CastTransmuteOp : Rust_PayloadOp<"cast_transmute", "MIR CastKind::Transmute">;
def Rust_CastSubtypeOp : Rust_PayloadOp<"cast_subtype", "MIR CastKind::Subtype">;

// Borrow and pointer kind variants.
def Rust_BorrowSharedOp : Rust_PayloadOp<"borrow_shared", "MIR BorrowKind::Shared">;
def Rust_BorrowFakeOp : Rust_PayloadOp<"borrow_fake", "MIR BorrowKind::Fake">;
def Rust_BorrowMutOp : Rust_PayloadOp<"borrow_mut", "MIR BorrowKind::Mut">;
def Rust_RawPtrMutOp : Rust_PayloadOp<"raw_ptr_mut", "MIR RawPtrKind::Mut">;
def Rust_RawPtrConstOp : Rust_PayloadOp<"raw_ptr_const", "MIR RawPtrKind::Const">;
def Rust_RawPtrFakeForPtrMetadataOp : Rust_PayloadOp<"raw_ptr_fake_for_ptr_metadata", "MIR RawPtrKind::FakeForPtrMetadata">;
def Rust_MutBorrowDefaultOp : Rust_PayloadOp<"mut_borrow_default", "MIR MutBorrowKind::Default">;
def Rust_MutBorrowTwoPhaseOp : Rust_PayloadOp<"mut_borrow_two_phase", "MIR MutBorrowKind::TwoPhaseBorrow">;
def Rust_MutBorrowClosureCaptureOp : Rust_PayloadOp<"mut_borrow_closure_capture", "MIR MutBorrowKind::ClosureCapture">;
def Rust_FakeBorrowDeepOp : Rust_PayloadOp<"fake_borrow_deep", "MIR FakeBorrowKind::Deep">;
def Rust_FakeBorrowShallowOp : Rust_PayloadOp<"fake_borrow_shallow", "MIR FakeBorrowKind::Shallow">;

// Statement payload enum variants.
def Rust_FakeReadForMatchGuardOp : Rust_PayloadOp<"fake_read_for_match_guard", "MIR FakeReadCause::ForMatchGuard">;
def Rust_FakeReadForMatchedPlaceOp : Rust_PayloadOp<"fake_read_for_matched_place", "MIR FakeReadCause::ForMatchedPlace">;
def Rust_FakeReadForGuardBindingOp : Rust_PayloadOp<"fake_read_for_guard_binding", "MIR FakeReadCause::ForGuardBinding">;
def Rust_FakeReadForLetOp : Rust_PayloadOp<"fake_read_for_let", "MIR FakeReadCause::ForLet">;
def Rust_FakeReadForIndexOp : Rust_PayloadOp<"fake_read_for_index", "MIR FakeReadCause::ForIndex">;
def Rust_RetagFnEntryOp : Rust_PayloadOp<"retag_fn_entry", "MIR RetagKind::FnEntry">;
def Rust_RetagTwoPhaseOp : Rust_PayloadOp<"retag_two_phase", "MIR RetagKind::TwoPhase">;
def Rust_RetagRawOp : Rust_PayloadOp<"retag_raw", "MIR RetagKind::Raw">;
def Rust_RetagDefaultOp : Rust_PayloadOp<"retag_default", "MIR RetagKind::Default">;
def Rust_VarianceCovariantOp : Rust_PayloadOp<"variance_covariant", "MIR Variance::Covariant">;
def Rust_VarianceInvariantOp : Rust_PayloadOp<"variance_invariant", "MIR Variance::Invariant">;
def Rust_VarianceContravariantOp : Rust_PayloadOp<"variance_contravariant", "MIR Variance::Contravariant">;
def Rust_VarianceBivariantOp : Rust_PayloadOp<"variance_bivariant", "MIR Variance::Bivariant">;
def Rust_IntrinsicAssumeOp : Rust_PayloadOp<"intrinsic_assume", "MIR NonDivergingIntrinsic::Assume">;
def Rust_IntrinsicCopyNonOverlappingOp : Rust_PayloadOp<"intrinsic_copy_nonoverlapping", "MIR NonDivergingIntrinsic::CopyNonOverlapping">;

// RuntimeChecks variants.
def Rust_RuntimeChecksUbOp : Rust_PayloadOp<"runtime_checks_ub", "MIR RuntimeChecks::UbChecks">;
def Rust_RuntimeChecksContractOp : Rust_PayloadOp<"runtime_checks_contract", "MIR RuntimeChecks::ContractChecks">;
def Rust_RuntimeChecksOverflowOp : Rust_PayloadOp<"runtime_checks_overflow", "MIR RuntimeChecks::OverflowChecks">;

// UnwindAction and AssertMessage variants.
def Rust_UnwindContinueOp : Rust_PayloadOp<"unwind_continue", "MIR UnwindAction::Continue">;
def Rust_UnwindUnreachableOp : Rust_PayloadOp<"unwind_unreachable", "MIR UnwindAction::Unreachable">;
def Rust_UnwindTerminateOp : Rust_PayloadOp<"unwind_terminate", "MIR UnwindAction::Terminate">;
def Rust_UnwindCleanupOp : Rust_PayloadOp<"unwind_cleanup", "MIR UnwindAction::Cleanup">;
def Rust_AssertBoundsCheckOp : Rust_PayloadOp<"assert_bounds_check", "MIR AssertMessage::BoundsCheck">;
def Rust_AssertOverflowOp : Rust_PayloadOp<"assert_overflow", "MIR AssertMessage::Overflow">;
def Rust_AssertOverflowNegOp : Rust_PayloadOp<"assert_overflow_neg", "MIR AssertMessage::OverflowNeg">;
def Rust_AssertDivisionByZeroOp : Rust_PayloadOp<"assert_division_by_zero", "MIR AssertMessage::DivisionByZero">;
def Rust_AssertRemainderByZeroOp : Rust_PayloadOp<"assert_remainder_by_zero", "MIR AssertMessage::RemainderByZero">;
def Rust_AssertResumedAfterReturnOp : Rust_PayloadOp<"assert_resumed_after_return", "MIR AssertMessage::ResumedAfterReturn">;
def Rust_AssertResumedAfterPanicOp : Rust_PayloadOp<"assert_resumed_after_panic", "MIR AssertMessage::ResumedAfterPanic">;
def Rust_AssertResumedAfterDropOp : Rust_PayloadOp<"assert_resumed_after_drop", "MIR AssertMessage::ResumedAfterDrop">;
def Rust_AssertMisalignedPointerDereferenceOp : Rust_PayloadOp<"assert_misaligned_pointer_dereference", "MIR AssertMessage::MisalignedPointerDereference">;
def Rust_AssertNullPointerDereferenceOp : Rust_PayloadOp<"assert_null_pointer_dereference", "MIR AssertMessage::NullPointerDereference">;
def Rust_AssertInvalidEnumConstructionOp : Rust_PayloadOp<"assert_invalid_enum_construction", "MIR AssertMessage::InvalidEnumConstruction">;

// Other MIR payload enum variants.
def Rust_MutabilityNotOp : Rust_PayloadOp<"mutability_not", "MIR Mutability::Not">;
def Rust_MutabilityMutOp : Rust_PayloadOp<"mutability_mut", "MIR Mutability::Mut">;
def Rust_SafetySafeOp : Rust_PayloadOp<"safety_safe", "MIR Safety::Safe">;
def Rust_SafetyUnsafeOp : Rust_PayloadOp<"safety_unsafe", "MIR Safety::Unsafe">;
def Rust_PointerCoercionReifyFnPointerOp : Rust_PayloadOp<"pointer_coercion_reify_fn_pointer", "MIR PointerCoercion::ReifyFnPointer">;
def Rust_PointerCoercionUnsafeFnPointerOp : Rust_PayloadOp<"pointer_coercion_unsafe_fn_pointer", "MIR PointerCoercion::UnsafeFnPointer">;
def Rust_PointerCoercionClosureFnPointerOp : Rust_PayloadOp<"pointer_coercion_closure_fn_pointer", "MIR PointerCoercion::ClosureFnPointer">;
def Rust_PointerCoercionMutToConstPointerOp : Rust_PayloadOp<"pointer_coercion_mut_to_const_pointer", "MIR PointerCoercion::MutToConstPointer">;
def Rust_PointerCoercionArrayToPointerOp : Rust_PayloadOp<"pointer_coercion_array_to_pointer", "MIR PointerCoercion::ArrayToPointer">;
def Rust_PointerCoercionUnsizeOp : Rust_PayloadOp<"pointer_coercion_unsize", "MIR PointerCoercion::Unsize">;
def Rust_CoroutineKindDesugaredOp : Rust_PayloadOp<"coroutine_kind_desugared", "MIR CoroutineKind::Desugared">;
def Rust_CoroutineKindCoroutineOp : Rust_PayloadOp<"coroutine_kind_coroutine", "MIR CoroutineKind::Coroutine">;
def Rust_CoroutineSourceBlockOp : Rust_PayloadOp<"coroutine_source_block", "MIR CoroutineSource::Block">;
def Rust_CoroutineSourceClosureOp : Rust_PayloadOp<"coroutine_source_closure", "MIR CoroutineSource::Closure">;
def Rust_CoroutineSourceFnOp : Rust_PayloadOp<"coroutine_source_fn", "MIR CoroutineSource::Fn">;
def Rust_CoroutineDesugaringAsyncOp : Rust_PayloadOp<"coroutine_desugaring_async", "MIR CoroutineDesugaring::Async">;
def Rust_CoroutineDesugaringGenOp : Rust_PayloadOp<"coroutine_desugaring_gen", "MIR CoroutineDesugaring::Gen">;
def Rust_CoroutineDesugaringAsyncGenOp : Rust_PayloadOp<"coroutine_desugaring_async_gen", "MIR CoroutineDesugaring::AsyncGen">;
def Rust_MovabilityStaticOp : Rust_PayloadOp<"movability_static", "MIR Coroutine Movability::Static">;
def Rust_MovabilityMovableOp : Rust_PayloadOp<"movability_movable", "MIR Coroutine Movability::Movable">;

#endif // MLIR_DIALECT_RUSTMIR_IR_RUSTOPS_TD
