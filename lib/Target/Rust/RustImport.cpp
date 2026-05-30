//===- RustImport.cpp - Import Rust MIR NDJSON into MLIR -------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RustToLLVM/Target/Rust/RustImport.h"

#include "mlir/Dialect/DLTI/DLTI.h"
#include "mlir/Dialect/RustMIR/IR/RustMIRDialect.h"
#include "mlir/Dialect/RustMIR/IR/RustTypes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Location.h"
#include "mlir/IR/Verifier.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

#include <cstring>
#include <optional>

using namespace mlir;
namespace rustmir = mlir::rust::mir;

namespace {

LogicalResult emitImportError(MLIRContext *context,
                              const llvm::Twine &message) {
  emitError(UnknownLoc::get(context)) << message;
  return failure();
}

bool isControlKey(llvm::StringRef key) {
  return key == "record" || key == "event" || key == "kind" || key == "op";
}

struct ParsedSpan {
  std::string filename;
  unsigned startLine = 0;
  unsigned startColumn = 0;
  unsigned endLine = 0;
  unsigned endColumn = 0;
  bool hasEnd = false;
};

std::optional<unsigned> parseUnsigned(llvm::StringRef text) {
  unsigned value = 0;
  if (text.getAsInteger(10, value))
    return std::nullopt;
  return value;
}

llvm::StringRef extractRustcSpanRepr(llvm::StringRef span) {
  span = span.trim();
  size_t reprPos = span.find("repr:");
  if (reprPos == llvm::StringRef::npos)
    return span;

  llvm::StringRef repr = span.drop_front(reprPos + strlen("repr:")).trim();
  if (!repr.consume_front("\""))
    return span;
  size_t endQuote = repr.find('"');
  if (endQuote == llvm::StringRef::npos)
    return span;
  return repr.take_front(endQuote);
}

std::optional<ParsedSpan> parseRustSpan(llvm::StringRef rawSpan) {
  llvm::StringRef span = extractRustcSpanRepr(rawSpan);
  SmallVector<llvm::StringRef, 6> matches;

  llvm::Regex rangeRegex(
      "^(.+):([0-9]+):([0-9]+):[[:space:]]*([0-9]+):([0-9]+)$");
  if (rangeRegex.match(span, &matches)) {
    auto startLine = parseUnsigned(matches[2]);
    auto startColumn = parseUnsigned(matches[3]);
    auto endLine = parseUnsigned(matches[4]);
    auto endColumn = parseUnsigned(matches[5]);
    if (!startLine || !startColumn || !endLine || !endColumn)
      return std::nullopt;
    return ParsedSpan{matches[1].str(), *startLine, *startColumn,
                      *endLine,         *endColumn, true};
  }

  llvm::Regex pointRegex("^(.+):([0-9]+):([0-9]+)$");
  if (pointRegex.match(span, &matches)) {
    auto line = parseUnsigned(matches[2]);
    auto column = parseUnsigned(matches[3]);
    if (!line || !column)
      return std::nullopt;
    return ParsedSpan{matches[1].str(), *line, *column, 0, 0, false};
  }

  return std::nullopt;
}

std::optional<Location>
parseLocationFromObject(MLIRContext *context,
                        const llvm::json::Object &object) {
  std::optional<llvm::StringRef> span = object.getString("span");
  if (!span)
    return std::nullopt;

  std::optional<ParsedSpan> parsed = parseRustSpan(*span);
  if (!parsed)
    return std::nullopt;

  if (parsed->hasEnd)
    return FileLineColRange::get(context, parsed->filename, parsed->startLine,
                                 parsed->startColumn, parsed->endLine,
                                 parsed->endColumn);
  return FileLineColLoc::get(context, parsed->filename, parsed->startLine,
                             parsed->startColumn);
}

StringRef classifyRustcPublicPrimitiveTy(llvm::StringRef spelling) {
  size_t kindPos = spelling.find("kind:");
  if (kindPos != StringRef::npos)
    spelling = spelling.drop_front(kindPos + strlen("kind:")).trim();

  if (spelling.starts_with("RigidTy(Bool)"))
    return "bool";
  if (spelling.starts_with("RigidTy(Int(I8))"))
    return "i8";
  if (spelling.starts_with("RigidTy(Int(I16))"))
    return "i16";
  if (spelling.starts_with("RigidTy(Int(I32))"))
    return "i32";
  if (spelling.starts_with("RigidTy(Int(I64))"))
    return "i64";
  if (spelling.starts_with("RigidTy(Int(I128))"))
    return "i128";
  if (spelling.starts_with("RigidTy(Int(Isize))"))
    return "isize";
  if (spelling.starts_with("RigidTy(Uint(U8))"))
    return "u8";
  if (spelling.starts_with("RigidTy(Uint(U16))"))
    return "u16";
  if (spelling.starts_with("RigidTy(Uint(U32))"))
    return "u32";
  if (spelling.starts_with("RigidTy(Uint(U64))"))
    return "u64";
  if (spelling.starts_with("RigidTy(Uint(U128))"))
    return "u128";
  if (spelling.starts_with("RigidTy(Uint(Usize))"))
    return "usize";
  return "";
}

Type classifyRustType(MLIRContext *context, llvm::StringRef spelling) {
  llvm::StringRef s = spelling.trim();
  StringRef primitive = classifyRustcPublicPrimitiveTy(s);
  if (!primitive.empty())
    s = primitive;

  if (s == "bool")
    return rustmir::BoolType::get(context);
  if (s == "char")
    return rustmir::CharType::get(context);
  if (s == "()" || s == "unit")
    return rustmir::UnitType::get(context);
  if (s == "!" || s == "never")
    return rustmir::NeverType::get(context);
  if (s == "i8" || s == "i16" || s == "i32" || s == "i64" || s == "i128" ||
      s == "isize" || s == "u8" || s == "u16" || s == "u32" || s == "u64" ||
      s == "u128" || s == "usize")
    return rustmir::IntType::get(context, s);
  if (s.starts_with("&"))
    return rustmir::RefType::get(context, s);
  if (s.starts_with("*const") || s.starts_with("*mut"))
    return rustmir::RawPtrType::get(context, s);
  if (s.starts_with("("))
    return rustmir::TupleType::get(context, s);
  if (s.starts_with("[") && s.contains(";"))
    return rustmir::ArrayType::get(context, s);
  if (s.starts_with("["))
    return rustmir::SliceType::get(context, s);
  if (s.starts_with("fn(") || s.starts_with("unsafe fn("))
    return rustmir::FnType::get(context, s);
  if (s.contains("::"))
    return rustmir::AdtType::get(context, s);
  return rustmir::OpaqueType::get(context, s);
}

Attribute jsonToAttr(OpBuilder &builder, const llvm::json::Value &value);

DictionaryAttr jsonObjectToDict(OpBuilder &builder,
                                const llvm::json::Object &object) {
  SmallVector<NamedAttribute> attrs;
  for (const auto &entry : object)
    attrs.push_back(
        builder.getNamedAttr(entry.first, jsonToAttr(builder, entry.second)));
  return builder.getDictionaryAttr(attrs);
}

ArrayAttr jsonArrayToAttr(OpBuilder &builder, const llvm::json::Array &array) {
  SmallVector<Attribute> attrs;
  for (const llvm::json::Value &entry : array)
    attrs.push_back(jsonToAttr(builder, entry));
  return builder.getArrayAttr(attrs);
}

Attribute jsonToAttr(OpBuilder &builder, const llvm::json::Value &value) {
  if (auto stringValue = value.getAsString())
    return builder.getStringAttr(*stringValue);
  if (auto boolValue = value.getAsBoolean())
    return builder.getBoolAttr(*boolValue);
  if (auto intValue = value.getAsInteger())
    return builder.getI64IntegerAttr(*intValue);
  if (auto numberValue = value.getAsNumber())
    return builder.getF64FloatAttr(*numberValue);
  if (const auto *array = value.getAsArray())
    return jsonArrayToAttr(builder, *array);
  if (const auto *object = value.getAsObject())
    return jsonObjectToDict(builder, *object);
  return builder.getUnitAttr();
}

StringRef mapStatementKind(StringRef kind) {
  return llvm::StringSwitch<StringRef>(kind)
      .Case("Assign", "rust.mir.assign")
      .Case("FakeRead", "rust.mir.fake_read")
      .Case("SetDiscriminant", "rust.mir.set_discriminant")
      .Case("Deinit", "rust.mir.deinit")
      .Case("StorageLive", "rust.mir.storage_live")
      .Case("StorageDead", "rust.mir.storage_dead")
      .Case("Retag", "rust.mir.retag")
      .Case("PlaceMention", "rust.mir.place_mention")
      .Case("AscribeUserType", "rust.mir.ascribe_user_type")
      .Case("Coverage", "rust.mir.coverage")
      .Case("Intrinsic", "rust.mir.intrinsic")
      .Case("ConstEvalCounter", "rust.mir.const_eval_counter")
      .Case("Nop", "rust.mir.nop")
      .Default("");
}

StringRef mapRvalueKind(StringRef kind) {
  return llvm::StringSwitch<StringRef>(kind)
      .Case("AddressOf", "rust.mir.address_of")
      .Case("Aggregate", "rust.mir.aggregate")
      .Case("BinaryOp", "rust.mir.binary_op")
      .Case("Cast", "rust.mir.cast")
      .Case("CheckedBinaryOp", "rust.mir.checked_binary_op")
      .Case("CopyForDeref", "rust.mir.copy_for_deref")
      .Case("Discriminant", "rust.mir.discriminant")
      .Case("Len", "rust.mir.len")
      .Case("Ref", "rust.mir.ref")
      .Case("Repeat", "rust.mir.repeat")
      .Case("ShallowInitBox", "rust.mir.shallow_init_box")
      .Case("ThreadLocalRef", "rust.mir.thread_local_ref")
      .Case("NullaryOp", "rust.mir.nullary_op")
      .Case("UnaryOp", "rust.mir.unary_op")
      .Case("Use", "rust.mir.use")
      .Default("");
}

StringRef mapTerminatorKind(StringRef kind) {
  return llvm::StringSwitch<StringRef>(kind)
      .Case("Goto", "rust.mir.goto")
      .Case("SwitchInt", "rust.mir.switch_int")
      .Case("Resume", "rust.mir.resume")
      .Case("Abort", "rust.mir.abort")
      .Case("Return", "rust.mir.return")
      .Case("Unreachable", "rust.mir.unreachable")
      .Case("Drop", "rust.mir.drop")
      .Case("Call", "rust.mir.call")
      .Case("Assert", "rust.mir.assert")
      .Case("InlineAsm", "rust.mir.inline_asm")
      .Default("");
}

StringRef mapOperandKind(StringRef kind) {
  return llvm::StringSwitch<StringRef>(kind)
      .Case("Copy", "rust.mir.copy")
      .Case("Move", "rust.mir.move")
      .Case("Const", "rust.mir.const")
      .Case("Constant", "rust.mir.constant")
      .Case("RuntimeChecks", "rust.mir.runtime_checks")
      .Default("");
}

StringRef mapAggregateKind(StringRef kind) {
  return llvm::StringSwitch<StringRef>(kind)
      .Case("Array", "rust.mir.aggregate_array")
      .Case("Tuple", "rust.mir.aggregate_tuple")
      .Case("Adt", "rust.mir.aggregate_adt")
      .Case("Closure", "rust.mir.aggregate_closure")
      .Case("Coroutine", "rust.mir.aggregate_coroutine")
      .Case("CoroutineClosure", "rust.mir.aggregate_coroutine_closure")
      .Case("RawPtr", "rust.mir.aggregate_raw_ptr")
      .Default("");
}

StringRef mapProjectionElemKind(StringRef kind) {
  return llvm::StringSwitch<StringRef>(kind)
      .Case("Deref", "rust.mir.projection_deref")
      .Case("Field", "rust.mir.projection_field")
      .Case("Index", "rust.mir.projection_index")
      .Case("ConstantIndex", "rust.mir.projection_constant_index")
      .Case("Subslice", "rust.mir.projection_subslice")
      .Case("Downcast", "rust.mir.projection_downcast")
      .Case("OpaqueCast", "rust.mir.projection_opaque_cast")
      .Default("");
}

StringRef mapBinOpKind(StringRef kind) {
  return llvm::StringSwitch<StringRef>(kind)
      .Case("Add", "rust.mir.binop_add")
      .Case("AddUnchecked", "rust.mir.binop_add_unchecked")
      .Case("Sub", "rust.mir.binop_sub")
      .Case("SubUnchecked", "rust.mir.binop_sub_unchecked")
      .Case("Mul", "rust.mir.binop_mul")
      .Case("MulUnchecked", "rust.mir.binop_mul_unchecked")
      .Case("Div", "rust.mir.binop_div")
      .Case("Rem", "rust.mir.binop_rem")
      .Case("BitXor", "rust.mir.binop_bit_xor")
      .Case("BitAnd", "rust.mir.binop_bit_and")
      .Case("BitOr", "rust.mir.binop_bit_or")
      .Case("Shl", "rust.mir.binop_shl")
      .Case("ShlUnchecked", "rust.mir.binop_shl_unchecked")
      .Case("Shr", "rust.mir.binop_shr")
      .Case("ShrUnchecked", "rust.mir.binop_shr_unchecked")
      .Case("Eq", "rust.mir.binop_eq")
      .Case("Lt", "rust.mir.binop_lt")
      .Case("Le", "rust.mir.binop_le")
      .Case("Ne", "rust.mir.binop_ne")
      .Case("Ge", "rust.mir.binop_ge")
      .Case("Gt", "rust.mir.binop_gt")
      .Case("Cmp", "rust.mir.binop_cmp")
      .Case("Offset", "rust.mir.binop_offset")
      .Default("");
}

StringRef mapUnOpKind(StringRef kind) {
  return llvm::StringSwitch<StringRef>(kind)
      .Case("Not", "rust.mir.unop_not")
      .Case("Neg", "rust.mir.unop_neg")
      .Case("PtrMetadata", "rust.mir.unop_ptr_metadata")
      .Default("");
}

StringRef mapCastKind(StringRef kind) {
  return llvm::StringSwitch<StringRef>(kind)
      .Case("PointerExposeAddress", "rust.mir.cast_pointer_expose_address")
      .Case("PointerWithExposedProvenance",
            "rust.mir.cast_pointer_with_exposed_provenance")
      .Case("PointerCoercion", "rust.mir.cast_pointer_coercion")
      .Case("IntToInt", "rust.mir.cast_int_to_int")
      .Case("FloatToInt", "rust.mir.cast_float_to_int")
      .Case("FloatToFloat", "rust.mir.cast_float_to_float")
      .Case("IntToFloat", "rust.mir.cast_int_to_float")
      .Case("PtrToPtr", "rust.mir.cast_ptr_to_ptr")
      .Case("FnPtrToPtr", "rust.mir.cast_fn_ptr_to_ptr")
      .Case("Transmute", "rust.mir.cast_transmute")
      .Case("Subtype", "rust.mir.cast_subtype")
      .Default("");
}

StringRef mapBorrowKind(StringRef kind) {
  return llvm::StringSwitch<StringRef>(kind)
      .Case("Shared", "rust.mir.borrow_shared")
      .Case("Fake", "rust.mir.borrow_fake")
      .Case("Mut", "rust.mir.borrow_mut")
      .Default("");
}

StringRef mapRawPtrKind(StringRef kind) {
  return llvm::StringSwitch<StringRef>(kind)
      .Case("Mut", "rust.mir.raw_ptr_mut")
      .Case("Const", "rust.mir.raw_ptr_const")
      .Case("FakeForPtrMetadata", "rust.mir.raw_ptr_fake_for_ptr_metadata")
      .Default("");
}

StringRef mapFakeReadCauseKind(StringRef kind) {
  return llvm::StringSwitch<StringRef>(kind)
      .Case("ForMatchGuard", "rust.mir.fake_read_for_match_guard")
      .Case("ForMatchedPlace", "rust.mir.fake_read_for_matched_place")
      .Case("ForGuardBinding", "rust.mir.fake_read_for_guard_binding")
      .Case("ForLet", "rust.mir.fake_read_for_let")
      .Case("ForIndex", "rust.mir.fake_read_for_index")
      .Default("");
}

StringRef mapRetagKind(StringRef kind) {
  return llvm::StringSwitch<StringRef>(kind)
      .Case("FnEntry", "rust.mir.retag_fn_entry")
      .Case("TwoPhase", "rust.mir.retag_two_phase")
      .Case("Raw", "rust.mir.retag_raw")
      .Case("Default", "rust.mir.retag_default")
      .Default("");
}

StringRef mapVarianceKind(StringRef kind) {
  return llvm::StringSwitch<StringRef>(kind)
      .Case("Covariant", "rust.mir.variance_covariant")
      .Case("Invariant", "rust.mir.variance_invariant")
      .Case("Contravariant", "rust.mir.variance_contravariant")
      .Case("Bivariant", "rust.mir.variance_bivariant")
      .Default("");
}

StringRef mapIntrinsicKind(StringRef kind) {
  return llvm::StringSwitch<StringRef>(kind)
      .Case("Assume", "rust.mir.intrinsic_assume")
      .Case("CopyNonOverlapping", "rust.mir.intrinsic_copy_nonoverlapping")
      .Default("");
}

StringRef mapRuntimeChecksKind(StringRef kind) {
  return llvm::StringSwitch<StringRef>(kind)
      .Case("UbChecks", "rust.mir.runtime_checks_ub")
      .Case("ContractChecks", "rust.mir.runtime_checks_contract")
      .Case("OverflowChecks", "rust.mir.runtime_checks_overflow")
      .Default("");
}

StringRef mapUnwindActionKind(StringRef kind) {
  return llvm::StringSwitch<StringRef>(kind)
      .Case("Continue", "rust.mir.unwind_continue")
      .Case("Unreachable", "rust.mir.unwind_unreachable")
      .Case("Terminate", "rust.mir.unwind_terminate")
      .Case("Cleanup", "rust.mir.unwind_cleanup")
      .Default("");
}

StringRef mapAssertMessageKind(StringRef kind) {
  return llvm::StringSwitch<StringRef>(kind)
      .Case("BoundsCheck", "rust.mir.assert_bounds_check")
      .Case("Overflow", "rust.mir.assert_overflow")
      .Case("OverflowNeg", "rust.mir.assert_overflow_neg")
      .Case("DivisionByZero", "rust.mir.assert_division_by_zero")
      .Case("RemainderByZero", "rust.mir.assert_remainder_by_zero")
      .Case("ResumedAfterReturn", "rust.mir.assert_resumed_after_return")
      .Case("ResumedAfterPanic", "rust.mir.assert_resumed_after_panic")
      .Case("ResumedAfterDrop", "rust.mir.assert_resumed_after_drop")
      .Case("MisalignedPointerDereference",
            "rust.mir.assert_misaligned_pointer_dereference")
      .Case("NullPointerDereference",
            "rust.mir.assert_null_pointer_dereference")
      .Case("InvalidEnumConstruction",
            "rust.mir.assert_invalid_enum_construction")
      .Default("");
}

StringRef mapMutBorrowKind(StringRef kind) {
  return llvm::StringSwitch<StringRef>(kind)
      .Case("Default", "rust.mir.mut_borrow_default")
      .Case("TwoPhaseBorrow", "rust.mir.mut_borrow_two_phase")
      .Case("ClosureCapture", "rust.mir.mut_borrow_closure_capture")
      .Default("");
}

StringRef mapFakeBorrowKind(StringRef kind) {
  return llvm::StringSwitch<StringRef>(kind)
      .Case("Deep", "rust.mir.fake_borrow_deep")
      .Case("Shallow", "rust.mir.fake_borrow_shallow")
      .Default("");
}

StringRef mapMutabilityKind(StringRef kind) {
  return llvm::StringSwitch<StringRef>(kind)
      .Case("Not", "rust.mir.mutability_not")
      .Case("Mut", "rust.mir.mutability_mut")
      .Default("");
}

StringRef mapSafetyKind(StringRef kind) {
  return llvm::StringSwitch<StringRef>(kind)
      .Case("Safe", "rust.mir.safety_safe")
      .Case("Unsafe", "rust.mir.safety_unsafe")
      .Default("");
}

StringRef mapPointerCoercionKind(StringRef kind) {
  return llvm::StringSwitch<StringRef>(kind)
      .Case("ReifyFnPointer", "rust.mir.pointer_coercion_reify_fn_pointer")
      .Case("UnsafeFnPointer", "rust.mir.pointer_coercion_unsafe_fn_pointer")
      .Case("ClosureFnPointer", "rust.mir.pointer_coercion_closure_fn_pointer")
      .Case("MutToConstPointer",
            "rust.mir.pointer_coercion_mut_to_const_pointer")
      .Case("ArrayToPointer", "rust.mir.pointer_coercion_array_to_pointer")
      .Case("Unsize", "rust.mir.pointer_coercion_unsize")
      .Default("");
}

StringRef mapCoroutineKind(StringRef kind) {
  return llvm::StringSwitch<StringRef>(kind)
      .Case("Desugared", "rust.mir.coroutine_kind_desugared")
      .Case("Coroutine", "rust.mir.coroutine_kind_coroutine")
      .Default("");
}

StringRef mapCoroutineSourceKind(StringRef kind) {
  return llvm::StringSwitch<StringRef>(kind)
      .Case("Block", "rust.mir.coroutine_source_block")
      .Case("Closure", "rust.mir.coroutine_source_closure")
      .Case("Fn", "rust.mir.coroutine_source_fn")
      .Default("");
}

StringRef mapCoroutineDesugaringKind(StringRef kind) {
  return llvm::StringSwitch<StringRef>(kind)
      .Case("Async", "rust.mir.coroutine_desugaring_async")
      .Case("Gen", "rust.mir.coroutine_desugaring_gen")
      .Case("AsyncGen", "rust.mir.coroutine_desugaring_async_gen")
      .Default("");
}

StringRef mapMovabilityKind(StringRef kind) {
  return llvm::StringSwitch<StringRef>(kind)
      .Case("Static", "rust.mir.movability_static")
      .Case("Movable", "rust.mir.movability_movable")
      .Default("");
}

bool isMirVariantRecordName(StringRef record) {
  return record == "statement" || record == "terminator" ||
         record == "rvalue" || record == "operand" || record == "place" ||
         record == "projection" || record == "aggregate_kind" ||
         record == "projection_elem" || record == "binop" || record == "unop" ||
         record == "cast_kind" || record == "borrow_kind" ||
         record == "raw_ptr_kind" || record == "fake_read_cause" ||
         record == "retag_kind" || record == "variance" ||
         record == "non_diverging_intrinsic" || record == "runtime_checks" ||
         record == "unwind_action" || record == "assert_message" ||
         record == "mut_borrow_kind" || record == "fake_borrow_kind" ||
         record == "mutability" || record == "safety" ||
         record == "pointer_coercion" || record == "coroutine_kind" ||
         record == "coroutine_source" || record == "coroutine_desugaring" ||
         record == "movability";
}

void addAttrsFromObject(OpBuilder &builder, OperationState &state,
                        const llvm::json::Object &object,
                        ArrayRef<StringRef> extraSkips = {}) {
  for (const auto &entry : object) {
    StringRef key = entry.first;
    if (isControlKey(key) || llvm::is_contained(extraSkips, key))
      continue;

    StringRef attrName = key;
    Attribute attr = jsonToAttr(builder, entry.second);
    if ((key == "ty" || key == "type" || key == "rust_type") &&
        entry.second.getAsString()) {
      attrName = "rust_type";
      attr = TypeAttr::get(
          classifyRustType(builder.getContext(), *entry.second.getAsString()));
    }
    state.addAttribute(attrName, attr);
  }
}

struct ImportState {
  explicit ImportState(MLIRContext *context)
      : context(context), builder(context),
        module(ModuleOp::create(UnknownLoc::get(context))),
        currentBodyLoc(UnknownLoc::get(context)) {
    builder.setInsertionPointToEnd(module->getBody());
  }

  MLIRContext *context;
  OpBuilder builder;
  OwningOpRef<ModuleOp> module;
  Block *functionBody = nullptr;
  Block *blockBody = nullptr;
  Location currentBodyLoc;
};

LogicalResult createSimpleOp(ImportState &state, StringRef opName,
                             const llvm::json::Object &object,
                             std::optional<StringRef> mirKind = std::nullopt) {
  if (opName.empty())
    return emitImportError(state.context, "unsupported MIR kind");

  std::optional<Location> explicitLoc =
      parseLocationFromObject(state.context, object);
  Location opLoc = explicitLoc.value_or(state.currentBodyLoc);
  OperationState opState(opLoc, opName);
  if (explicitLoc)
    state.currentBodyLoc = *explicitLoc;
  addAttrsFromObject(state.builder, opState, object);
  if (mirKind)
    opState.addAttribute("mir_kind", state.builder.getStringAttr(*mirKind));

  if (!opState.attributes.get("payload")) {
    SmallVector<NamedAttribute> payloadAttrs;
    for (const auto &entry : object) {
      if (isControlKey(entry.first) || entry.first == "span")
        continue;
      payloadAttrs.push_back(state.builder.getNamedAttr(
          entry.first, jsonToAttr(state.builder, entry.second)));
    }
    if (!payloadAttrs.empty())
      opState.addAttribute("payload",
                           state.builder.getDictionaryAttr(payloadAttrs));
  }

  state.builder.create(opState);
  return success();
}

LogicalResult handleModuleRecord(ImportState &state,
                                 const llvm::json::Object &object) {
  if (std::optional<Location> loc =
          parseLocationFromObject(state.context, object))
    state.module->getOperation()->setLoc(*loc);

  for (const auto &entry : object) {
    if (isControlKey(entry.first))
      continue;
    StringRef key = entry.first;
    std::string name = ("rust.mir." + key).str();
    state.module->getOperation()->setAttr(
        name, jsonToAttr(state.builder, entry.second));
  }

  std::optional<int64_t> pointerWidth =
      object.getInteger("target_pointer_width");
  if (pointerWidth && *pointerWidth > 0) {
    SmallVector<DataLayoutEntryInterface> entries;
    entries.push_back(DataLayoutEntryAttr::get(
        IndexType::get(state.context),
        state.builder.getI32IntegerAttr(static_cast<int32_t>(*pointerWidth))));

    if (std::optional<StringRef> endian = object.getString("target_endian")) {
      entries.push_back(DataLayoutEntryAttr::get(
          state.builder.getStringAttr(DLTIDialect::kDataLayoutEndiannessKey),
          state.builder.getStringAttr(*endian)));
    }

    state.module->getOperation()->setAttr(
        DLTIDialect::kDataLayoutAttrName,
        DataLayoutSpecAttr::get(state.context, entries));
  }
  return success();
}

LogicalResult handleFunctionRecord(ImportState &state,
                                   const llvm::json::Object &object) {
  std::optional<StringRef> event = object.getString("event");
  if (!event &&
      object.getString("record") == std::optional<StringRef>("function_begin"))
    event = "begin";
  if (!event &&
      object.getString("record") == std::optional<StringRef>("function_end"))
    event = "end";

  if (!event)
    return emitImportError(state.context, "function record is missing event");

  if (*event == "end") {
    state.functionBody = nullptr;
    state.blockBody = nullptr;
    state.builder.setInsertionPointToEnd(state.module->getBody());
    return success();
  }

  if (*event != "begin")
    return emitImportError(state.context,
                           "unsupported function event: " + *event);

  std::optional<StringRef> name = object.getString("name");
  if (!name)
    name = object.getString("sym_name");
  if (!name)
    return emitImportError(state.context,
                           "function begin record is missing name");

  Location loc = parseLocationFromObject(state.context, object)
                     .value_or(UnknownLoc::get(state.context));
  OperationState opState(loc, "rust.mir.func");
  opState.addAttribute("sym_name", state.builder.getStringAttr(*name));
  opState.addAttribute("rust_name", state.builder.getStringAttr(*name));
  addAttrsFromObject(state.builder, opState, object, {"name", "sym_name"});
  opState.addRegion();

  Operation *func = state.builder.create(opState);
  Region &body = func->getRegion(0);
  body.push_back(new Block());
  state.functionBody = &body.front();
  state.blockBody = nullptr;
  state.builder.setInsertionPointToEnd(state.functionBody);
  return success();
}

LogicalResult handleLocalRecord(ImportState &state,
                                const llvm::json::Object &object) {
  if (!state.functionBody)
    return emitImportError(state.context, "local record outside a function");

  Location loc = parseLocationFromObject(state.context, object)
                     .value_or(UnknownLoc::get(state.context));
  OperationState opState(loc, "rust.mir.local");
  addAttrsFromObject(state.builder, opState, object);
  if (!opState.attributes.get("index"))
    return emitImportError(state.context, "local record is missing index");

  state.builder.create(opState);
  return success();
}

LogicalResult handleBlockRecord(ImportState &state,
                                const llvm::json::Object &object) {
  std::optional<StringRef> event = object.getString("event");
  if (!event &&
      object.getString("record") == std::optional<StringRef>("block_begin"))
    event = "begin";
  if (!event &&
      object.getString("record") == std::optional<StringRef>("block_end"))
    event = "end";

  if (!event)
    return emitImportError(state.context, "block record is missing event");

  if (*event == "end") {
    state.blockBody = nullptr;
    state.currentBodyLoc = UnknownLoc::get(state.context);
    if (!state.functionBody)
      return emitImportError(state.context, "block end outside a function");
    state.builder.setInsertionPointToEnd(state.functionBody);
    return success();
  }

  if (*event != "begin")
    return emitImportError(state.context, "unsupported block event: " + *event);
  if (!state.functionBody)
    return emitImportError(state.context, "block begin outside a function");

  Location loc = parseLocationFromObject(state.context, object)
                     .value_or(UnknownLoc::get(state.context));
  state.currentBodyLoc = loc;
  OperationState opState(loc, "rust.mir.block");
  addAttrsFromObject(state.builder, opState, object);
  if (!opState.attributes.get("index"))
    return emitImportError(state.context, "block record is missing index");
  opState.addRegion();

  Operation *block = state.builder.create(opState);
  Region &body = block->getRegion(0);
  body.push_back(new Block());
  state.blockBody = &body.front();
  state.builder.setInsertionPointToEnd(state.blockBody);
  return success();
}

LogicalResult handleMirVariantRecord(ImportState &state,
                                     const llvm::json::Object &object,
                                     StringRef record) {
  if (!state.blockBody && isMirVariantRecordName(record))
    return emitImportError(state.context, "MIR body record outside a block");

  if (auto op = object.getString("op"))
    return createSimpleOp(state, *op, object);

  std::optional<StringRef> kind = object.getString("kind");
  if (!kind)
    return emitImportError(state.context, "MIR variant record is missing kind");

  StringRef opName;
  if (record == "statement")
    opName = mapStatementKind(*kind);
  else if (record == "terminator")
    opName = mapTerminatorKind(*kind);
  else if (record == "rvalue")
    opName = mapRvalueKind(*kind);
  else if (record == "operand")
    opName = mapOperandKind(*kind);
  else if (record == "place")
    opName = "rust.mir.place";
  else if (record == "projection")
    opName = "rust.mir.projection";
  else if (record == "aggregate_kind")
    opName = mapAggregateKind(*kind);
  else if (record == "projection_elem")
    opName = mapProjectionElemKind(*kind);
  else if (record == "binop")
    opName = mapBinOpKind(*kind);
  else if (record == "unop")
    opName = mapUnOpKind(*kind);
  else if (record == "cast_kind")
    opName = mapCastKind(*kind);
  else if (record == "borrow_kind")
    opName = mapBorrowKind(*kind);
  else if (record == "raw_ptr_kind")
    opName = mapRawPtrKind(*kind);
  else if (record == "fake_read_cause")
    opName = mapFakeReadCauseKind(*kind);
  else if (record == "retag_kind")
    opName = mapRetagKind(*kind);
  else if (record == "variance")
    opName = mapVarianceKind(*kind);
  else if (record == "non_diverging_intrinsic")
    opName = mapIntrinsicKind(*kind);
  else if (record == "runtime_checks")
    opName = mapRuntimeChecksKind(*kind);
  else if (record == "unwind_action")
    opName = mapUnwindActionKind(*kind);
  else if (record == "assert_message")
    opName = mapAssertMessageKind(*kind);
  else if (record == "mut_borrow_kind")
    opName = mapMutBorrowKind(*kind);
  else if (record == "fake_borrow_kind")
    opName = mapFakeBorrowKind(*kind);
  else if (record == "mutability")
    opName = mapMutabilityKind(*kind);
  else if (record == "safety")
    opName = mapSafetyKind(*kind);
  else if (record == "pointer_coercion")
    opName = mapPointerCoercionKind(*kind);
  else if (record == "coroutine_kind")
    opName = mapCoroutineKind(*kind);
  else if (record == "coroutine_source")
    opName = mapCoroutineSourceKind(*kind);
  else if (record == "coroutine_desugaring")
    opName = mapCoroutineDesugaringKind(*kind);
  else if (record == "movability")
    opName = mapMovabilityKind(*kind);

  if (opName.empty())
    return emitImportError(state.context,
                           "unsupported " + record + " kind: " + *kind);
  return createSimpleOp(state, opName, object, *kind);
}

LogicalResult handleRecord(ImportState &state,
                           const llvm::json::Object &object) {
  std::optional<StringRef> record = object.getString("record");
  if (!record)
    return emitImportError(state.context, "NDJSON record is missing 'record'");

  if (*record == "module")
    return handleModuleRecord(state, object);
  if (*record == "function" || *record == "function_begin" ||
      *record == "function_end")
    return handleFunctionRecord(state, object);
  if (*record == "local")
    return handleLocalRecord(state, object);
  if (*record == "block" || *record == "block_begin" || *record == "block_end")
    return handleBlockRecord(state, object);
  if (isMirVariantRecordName(*record))
    return handleMirVariantRecord(state, object, *record);

  return emitImportError(state.context,
                         "unsupported NDJSON record: " + *record);
}

} // namespace

OwningOpRef<Operation *> mlir::rust::importRustNdjson(StringRef input,
                                                      MLIRContext *context) {
  context->loadDialect<DLTIDialect>();
  context->loadDialect<rustmir::RustMIRDialect>();
  ImportState state(context);

  unsigned lineNumber = 0;
  for (StringRef line : llvm::split(input, '\n')) {
    ++lineNumber;
    line = line.trim();
    if (line.empty() || line.starts_with("//"))
      continue;

    llvm::Expected<llvm::json::Value> parsed = llvm::json::parse(line);
    if (!parsed) {
      std::string message = "invalid JSON on line " +
                            std::to_string(lineNumber) + ": " +
                            llvm::toString(parsed.takeError());
      (void)emitImportError(context, message);
      return nullptr;
    }

    const llvm::json::Object *object = parsed->getAsObject();
    if (!object) {
      (void)emitImportError(context, "NDJSON line " + Twine(lineNumber) +
                                         " is not an object");
      return nullptr;
    }

    if (failed(handleRecord(state, *object)))
      return nullptr;
  }

  if (failed(verify(*state.module)))
    return nullptr;

  return std::move(state.module);
}

OwningOpRef<Operation *>
mlir::rust::importRustNdjson(llvm::SourceMgr &sourceMgr, MLIRContext *context) {
  const llvm::MemoryBuffer *buffer =
      sourceMgr.getMemoryBuffer(sourceMgr.getMainFileID());
  return importRustNdjson(buffer->getBuffer(), context);
}
