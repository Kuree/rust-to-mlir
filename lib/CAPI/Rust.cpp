//===- Rust.cpp - C API for the Rust dialect --------------------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir-c/Dialect/Rust.h"

#include "mlir/Bytecode/BytecodeWriter.h"
#include "mlir/CAPI/IR.h"
#include "mlir/CAPI/Registration.h"
#include "mlir/CAPI/Support.h"
#include "mlir/Dialect/DLTI/DLTI.h"
#include "mlir/Dialect/RustMIR/IR/RustMIRDialect.h"
#include "mlir/Dialect/RustMIR/IR/RustOps.h"
#include "mlir/Dialect/RustMIR/IR/RustTypes.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Location.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/OperationSupport.h"
#include "mlir/Parser/Parser.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Verifier.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/raw_ostream.h"

#include <cstring>
#include <optional>
#include <string>

using namespace mlir;
namespace rustmir = mlir::rust::mir;

MLIR_DEFINE_CAPI_DIALECT_REGISTRATION(RustMIR, rust,
                                      mlir::rust::mir::RustMIRDialect)

namespace {
constexpr llvm::StringLiteral kRustMIRFunc("rust.mir.func");
constexpr llvm::StringLiteral kRustMIRLocal("rust.mir.local");
constexpr llvm::StringLiteral kRustMIRBlock("rust.mir.block");
constexpr llvm::StringLiteral kRustMIRAssign("rust.mir.assign");
constexpr llvm::StringLiteral kRustMIRReturn("rust.mir.return");

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
  if (s.starts_with("(") || s.contains("RigidTy(Tuple("))
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

StringAttr stringAttr(MLIRContext *context, MlirStringRef string) {
  return StringAttr::get(context, unwrap(string));
}

void addStringAttr(MLIRContext *context, OperationState &state, StringRef name,
                   MlirStringRef value) {
  if (!value.data && value.length == 0)
    return;
  state.addAttribute(name, stringAttr(context, value));
}

NamedAttribute named(Builder &builder, StringRef name, Attribute attr) {
  return builder.getNamedAttr(name, attr);
}

DictionaryAttr dictionary(MLIRContext *context,
                          ArrayRef<NamedAttribute> attrs) {
  return DictionaryAttr::get(context, attrs);
}

MlirAttribute wrapDictionary(MLIRContext *context,
                             ArrayRef<NamedAttribute> attrs) {
  return wrap(dictionary(context, attrs));
}

MlirOperation createOperation(OperationState &state) {
  return wrap(Operation::create(state));
}
} // namespace

MlirContext rustMlirContextCreate(void) {
  auto *context = new MLIRContext();
  context->loadDialect<rustmir::RustMIRDialect, DLTIDialect>();
  return wrap(context);
}

void rustMlirContextDestroy(MlirContext context) { delete unwrap(context); }

MlirLocation rustMlirLocationUnknownGet(MlirContext context) {
  Location loc = UnknownLoc::get(unwrap(context));
  return wrap(loc);
}

MlirLocation rustMlirLocationFromRustSpan(MlirContext context,
                                          MlirStringRef span) {
  MLIRContext *ctx = unwrap(context);
  std::optional<ParsedSpan> parsed = parseRustSpan(unwrap(span));
  if (!parsed) {
    Location loc = UnknownLoc::get(ctx);
    return wrap(loc);
  }

  if (parsed->hasEnd) {
    Location loc = FileLineColRange::get(ctx, parsed->filename,
                                         parsed->startLine, parsed->startColumn,
                                         parsed->endLine, parsed->endColumn);
    return wrap(loc);
  }
  Location loc = FileLineColLoc::get(ctx, parsed->filename, parsed->startLine,
                                     parsed->startColumn);
  return wrap(loc);
}

MlirModule rustMlirModuleCreate(MlirLocation location) {
  return wrap(ModuleOp::create(unwrap(location)));
}

void rustMlirModuleDestroy(MlirModule module) { unwrap(module).erase(); }

MlirOperation rustMlirModuleGetOperation(MlirModule module) {
  return wrap(unwrap(module).getOperation());
}

MlirBlock rustMlirModuleGetBody(MlirModule module) {
  return wrap(unwrap(module).getBody());
}

void rustMlirBlockAppendOwnedOperation(MlirBlock block, MlirOperation op) {
  unwrap(block)->push_back(unwrap(op));
}

bool rustMlirOperationVerify(MlirOperation op) {
  return succeeded(verify(unwrap(op)));
}

bool rustMlirWriteBytecodeToFile(MlirOperation op, MlirStringRef path) {
  llvm::StringRef outputPath = unwrap(path);
  if (outputPath == "-")
    return succeeded(writeBytecodeToFile(unwrap(op), llvm::outs()));

  std::error_code error;
  llvm::raw_fd_ostream output(outputPath, error, llvm::sys::fs::OF_None);
  if (error)
    return false;
  return succeeded(writeBytecodeToFile(unwrap(op), output));
}

bool rustMlirWriteTextToFile(MlirOperation op, MlirStringRef path) {
  llvm::StringRef outputPath = unwrap(path);
  if (outputPath == "-") {
    unwrap(op)->print(llvm::outs());
    llvm::outs() << "\n";
    return !llvm::outs().has_error();
  }

  std::error_code error;
  llvm::raw_fd_ostream output(outputPath, error, llvm::sys::fs::OF_Text);
  if (error)
    return false;
  unwrap(op)->print(output);
  output << "\n";
  return !output.has_error();
}

bool rustMlirMergeTextModulesToFile(intptr_t numInputs,
                                    MlirStringRef const *inputPaths,
                                    MlirStringRef path, bool emitBytecode) {
  MLIRContext context;
  context.loadDialect<rustmir::RustMIRDialect, DLTIDialect>();

  OwningOpRef<ModuleOp> merged =
      ModuleOp::create(UnknownLoc::get(&context));
  bool copiedModuleAttrs = false;
  for (intptr_t i = 0; i < numInputs; ++i) {
    OwningOpRef<ModuleOp> input =
        parseSourceFile<ModuleOp>(unwrap(inputPaths[i]), &context);
    if (!input)
      return false;

    if (!copiedModuleAttrs) {
      for (NamedAttribute attr : input->getOperation()->getAttrs())
        merged->getOperation()->setAttr(attr.getName(), attr.getValue());
      copiedModuleAttrs = true;
    }

    for (Operation &op : *input->getBody())
      merged->getBody()->push_back(op.clone());
  }

  if (failed(verify(*merged)))
    return false;

  llvm::StringRef outputPath = unwrap(path);
  if (outputPath == "-") {
    if (emitBytecode) {
      if (failed(writeBytecodeToFile(merged->getOperation(), llvm::outs())))
        return false;
    } else {
      merged->print(llvm::outs());
      llvm::outs() << "\n";
    }
    return !llvm::outs().has_error();
  }

  std::error_code error;
  llvm::raw_fd_ostream output(outputPath, error,
                              emitBytecode ? llvm::sys::fs::OF_None
                                           : llvm::sys::fs::OF_Text);
  if (error)
    return false;
  if (emitBytecode) {
    if (failed(writeBytecodeToFile(merged->getOperation(), output)))
      return false;
  } else {
    merged->print(output);
    output << "\n";
  }
  return !output.has_error();
}

void rustMirModuleSetTarget(MlirModule module, int64_t pointerWidth,
                            MlirStringRef endian) {
  ModuleOp op = unwrap(module);
  MLIRContext *context = op.getContext();
  Builder builder(context);

  op->setAttr("rust.mir.source", builder.getStringAttr("rustc_public"));
  op->setAttr("rust.mir.target_pointer_width",
              builder.getI64IntegerAttr(pointerWidth));
  op->setAttr("rust.mir.target_endian", stringAttr(context, endian));

  SmallVector<DataLayoutEntryInterface> entries;
  entries.push_back(DataLayoutEntryAttr::get(
      IndexType::get(context),
      builder.getI32IntegerAttr(static_cast<int32_t>(pointerWidth))));
  entries.push_back(DataLayoutEntryAttr::get(
      builder.getStringAttr(DLTIDialect::kDataLayoutEndiannessKey),
      stringAttr(context, endian)));
  op->setAttr(DLTIDialect::kDataLayoutAttrName,
              DataLayoutSpecAttr::get(context, entries));
}

MlirType rustMirTypeFromRustcPublicString(MlirContext context,
                                          MlirStringRef spelling) {
  return wrap(classifyRustType(unwrap(context), unwrap(spelling)));
}

MlirType rustMirBoolTypeGet(MlirContext context) {
  return wrap(rustmir::BoolType::get(unwrap(context)));
}

MlirType rustMirIntTypeGet(MlirContext context, MlirStringRef spelling) {
  return wrap(rustmir::IntType::get(unwrap(context), unwrap(spelling)));
}

MlirType rustMirUnitTypeGet(MlirContext context) {
  return wrap(rustmir::UnitType::get(unwrap(context)));
}

MlirType rustMirOpaqueTypeGet(MlirContext context, MlirStringRef spelling) {
  return wrap(rustmir::OpaqueType::get(unwrap(context), unwrap(spelling)));
}

MlirType rustTypedSlotTypeGet(MlirContext context, MlirType elementType) {
  return wrap(rustmir::SlotType::get(unwrap(context), unwrap(elementType)));
}

MlirType rustTypedTupleTypeGet(MlirContext context, intptr_t numElementTypes,
                               MlirType const *elementTypes) {
  MLIRContext *ctx = unwrap(context);
  SmallVector<Type> types;
  types.reserve(numElementTypes);
  for (intptr_t i = 0; i < numElementTypes; ++i)
    types.push_back(unwrap(elementTypes[i]));
  return wrap(rustmir::TypedTupleType::get(ctx, ArrayRef<Type>(types)));
}

MlirAttribute rustMirProjectionAttrGet(MlirContext context, MlirStringRef kind,
                                       MlirStringRef debug) {
  MLIRContext *ctx = unwrap(context);
  Builder builder(ctx);
  SmallVector<NamedAttribute> attrs;
  attrs.push_back(named(builder, "kind", stringAttr(ctx, kind)));
  attrs.push_back(named(builder, "debug", stringAttr(ctx, debug)));
  return wrapDictionary(ctx, attrs);
}

MlirAttribute rustMirProjectionFieldAttrGet(MlirContext context, int64_t index,
                                            MlirStringRef type) {
  MLIRContext *ctx = unwrap(context);
  Builder builder(ctx);
  SmallVector<NamedAttribute> attrs;
  attrs.push_back(named(builder, "kind", builder.getStringAttr("Field")));
  attrs.push_back(named(builder, "index", builder.getI64IntegerAttr(index)));
  attrs.push_back(named(builder, "ty", stringAttr(ctx, type)));
  return wrapDictionary(ctx, attrs);
}

MlirAttribute rustMirPlaceAttrGet(MlirContext context, int64_t local,
                                  intptr_t numProjections,
                                  MlirAttribute const *projections) {
  MLIRContext *ctx = unwrap(context);
  Builder builder(ctx);
  SmallVector<Attribute> projectionAttrs;
  projectionAttrs.reserve(numProjections);
  for (intptr_t i = 0; i < numProjections; ++i)
    projectionAttrs.push_back(unwrap(projections[i]));

  SmallVector<NamedAttribute> attrs;
  attrs.push_back(named(builder, "local", builder.getI64IntegerAttr(local)));
  attrs.push_back(
      named(builder, "projection", builder.getArrayAttr(projectionAttrs)));
  return wrapDictionary(ctx, attrs);
}

MlirAttribute rustMirOperandCopyAttrGet(MlirContext context,
                                        MlirAttribute place) {
  MLIRContext *ctx = unwrap(context);
  Builder builder(ctx);
  SmallVector<NamedAttribute> attrs;
  attrs.push_back(named(builder, "kind", builder.getStringAttr("Copy")));
  attrs.push_back(named(builder, "place", unwrap(place)));
  return wrapDictionary(ctx, attrs);
}

MlirAttribute rustMirOperandMoveAttrGet(MlirContext context,
                                        MlirAttribute place) {
  MLIRContext *ctx = unwrap(context);
  Builder builder(ctx);
  SmallVector<NamedAttribute> attrs;
  attrs.push_back(named(builder, "kind", builder.getStringAttr("Move")));
  attrs.push_back(named(builder, "place", unwrap(place)));
  return wrapDictionary(ctx, attrs);
}

MlirAttribute rustMirOperandConstantI64AttrGet(MlirContext context,
                                               int64_t value,
                                               MlirStringRef debug,
                                               MlirStringRef type) {
  MLIRContext *ctx = unwrap(context);
  Builder builder(ctx);
  SmallVector<NamedAttribute> attrs;
  attrs.push_back(named(builder, "kind", builder.getStringAttr("Constant")));
  attrs.push_back(named(builder, "value", builder.getI64IntegerAttr(value)));
  attrs.push_back(named(builder, "debug", stringAttr(ctx, debug)));
  attrs.push_back(named(builder, "ty", stringAttr(ctx, type)));
  return wrapDictionary(ctx, attrs);
}

MlirAttribute rustMirOperandDebugAttrGet(MlirContext context,
                                         MlirStringRef kind,
                                         MlirStringRef debug) {
  MLIRContext *ctx = unwrap(context);
  Builder builder(ctx);
  SmallVector<NamedAttribute> attrs;
  attrs.push_back(named(builder, "kind", stringAttr(ctx, kind)));
  attrs.push_back(named(builder, "debug", stringAttr(ctx, debug)));
  return wrapDictionary(ctx, attrs);
}

MlirAttribute rustMirRvalueBinaryOpAttrGet(MlirContext context,
                                           MlirStringRef kind, MlirStringRef op,
                                           MlirAttribute lhs,
                                           MlirAttribute rhs) {
  MLIRContext *ctx = unwrap(context);
  Builder builder(ctx);
  SmallVector<NamedAttribute> attrs;
  attrs.push_back(named(builder, "kind", stringAttr(ctx, kind)));
  attrs.push_back(named(builder, "op", stringAttr(ctx, op)));
  attrs.push_back(named(builder, "lhs", unwrap(lhs)));
  attrs.push_back(named(builder, "rhs", unwrap(rhs)));
  return wrapDictionary(ctx, attrs);
}

MlirAttribute rustMirRvalueUnaryOpAttrGet(MlirContext context,
                                          MlirStringRef kind, MlirStringRef op,
                                          MlirAttribute operand) {
  MLIRContext *ctx = unwrap(context);
  Builder builder(ctx);
  SmallVector<NamedAttribute> attrs;
  attrs.push_back(named(builder, "kind", stringAttr(ctx, kind)));
  attrs.push_back(named(builder, "op", stringAttr(ctx, op)));
  attrs.push_back(named(builder, "operand", unwrap(operand)));
  return wrapDictionary(ctx, attrs);
}

MlirAttribute rustMirRvalueAggregateAttrGet(MlirContext context,
                                            MlirStringRef kind,
                                            MlirStringRef aggregateKind,
                                            intptr_t numOperands,
                                            MlirAttribute const *operands) {
  MLIRContext *ctx = unwrap(context);
  Builder builder(ctx);
  SmallVector<Attribute> operandAttrs;
  operandAttrs.reserve(numOperands);
  for (intptr_t i = 0; i < numOperands; ++i)
    operandAttrs.push_back(unwrap(operands[i]));

  SmallVector<NamedAttribute> attrs;
  attrs.push_back(named(builder, "kind", stringAttr(ctx, kind)));
  attrs.push_back(
      named(builder, "aggregate_kind", stringAttr(ctx, aggregateKind)));
  attrs.push_back(
      named(builder, "operands", builder.getArrayAttr(operandAttrs)));
  return wrapDictionary(ctx, attrs);
}

MlirAttribute rustMirRvalueUseAttrGet(MlirContext context,
                                      MlirAttribute operand) {
  MLIRContext *ctx = unwrap(context);
  Builder builder(ctx);
  SmallVector<NamedAttribute> attrs;
  attrs.push_back(named(builder, "kind", builder.getStringAttr("Use")));
  attrs.push_back(named(builder, "operand", unwrap(operand)));
  return wrapDictionary(ctx, attrs);
}

MlirAttribute rustMirRvalueDebugAttrGet(MlirContext context, MlirStringRef kind,
                                        MlirStringRef debug) {
  MLIRContext *ctx = unwrap(context);
  Builder builder(ctx);
  SmallVector<NamedAttribute> attrs;
  attrs.push_back(named(builder, "kind", stringAttr(ctx, kind)));
  attrs.push_back(named(builder, "debug", stringAttr(ctx, debug)));
  return wrapDictionary(ctx, attrs);
}

MlirAttribute rustMirAssignPayloadAttrGet(MlirContext context,
                                          MlirAttribute place,
                                          MlirAttribute rvalue) {
  MLIRContext *ctx = unwrap(context);
  Builder builder(ctx);
  SmallVector<NamedAttribute> attrs;
  attrs.push_back(named(builder, "place", unwrap(place)));
  attrs.push_back(named(builder, "rvalue", unwrap(rvalue)));
  return wrapDictionary(ctx, attrs);
}

MlirAttribute rustMirTargetPayloadAttrGet(MlirContext context,
                                          MlirStringRef kind, int64_t target,
                                          MlirStringRef debug) {
  MLIRContext *ctx = unwrap(context);
  Builder builder(ctx);
  SmallVector<NamedAttribute> attrs;
  attrs.push_back(named(builder, "kind", stringAttr(ctx, kind)));
  attrs.push_back(named(builder, "target", builder.getI64IntegerAttr(target)));
  if (debug.data || debug.length != 0)
    attrs.push_back(named(builder, "debug", stringAttr(ctx, debug)));
  return wrapDictionary(ctx, attrs);
}

MlirAttribute rustMirSwitchTargetsAttrGet(MlirContext context,
                                          int64_t otherwise,
                                          intptr_t numBranches,
                                          int64_t const *values,
                                          int64_t const *targets) {
  MLIRContext *ctx = unwrap(context);
  Builder builder(ctx);
  SmallVector<NamedAttribute> attrs;
  attrs.push_back(
      named(builder, "default", builder.getI64IntegerAttr(otherwise)));
  for (intptr_t i = 0; i < numBranches; ++i) {
    std::string name = "case_" + std::to_string(values[i]);
    attrs.push_back(
        named(builder, name, builder.getI64IntegerAttr(targets[i])));
  }
  return wrapDictionary(ctx, attrs);
}

MlirAttribute rustMirSwitchIntPayloadAttrGet(MlirContext context,
                                             MlirAttribute discr,
                                             MlirAttribute targets,
                                             MlirStringRef debug) {
  MLIRContext *ctx = unwrap(context);
  Builder builder(ctx);
  SmallVector<NamedAttribute> attrs;
  attrs.push_back(named(builder, "kind", builder.getStringAttr("SwitchInt")));
  attrs.push_back(named(builder, "discr", unwrap(discr)));
  attrs.push_back(named(builder, "targets", unwrap(targets)));
  if (debug.data || debug.length != 0)
    attrs.push_back(named(builder, "debug", stringAttr(ctx, debug)));
  return wrapDictionary(ctx, attrs);
}

MlirAttribute rustMirAssertPayloadAttrGet(MlirContext context,
                                          MlirAttribute cond, bool expected,
                                          int64_t target, MlirStringRef debug) {
  MLIRContext *ctx = unwrap(context);
  Builder builder(ctx);
  SmallVector<NamedAttribute> attrs;
  attrs.push_back(named(builder, "kind", builder.getStringAttr("Assert")));
  attrs.push_back(named(builder, "cond", unwrap(cond)));
  attrs.push_back(named(builder, "expected", builder.getBoolAttr(expected)));
  attrs.push_back(named(builder, "target", builder.getI64IntegerAttr(target)));
  if (debug.data || debug.length != 0)
    attrs.push_back(named(builder, "debug", stringAttr(ctx, debug)));
  return wrapDictionary(ctx, attrs);
}

MlirOperation rustMirFuncCreate(MlirLocation location, MlirStringRef symName,
                                MlirStringRef rustName, MlirStringRef signature,
                                int64_t argCount, MlirStringRef itemKind) {
  MLIRContext *context = unwrap(location).getContext();
  Builder builder(context);
  OperationState state(unwrap(location), kRustMIRFunc);
  state.addAttribute(SymbolTable::getSymbolAttrName(),
                     stringAttr(context, symName));
  state.addAttribute("rust_name", stringAttr(context, rustName));
  state.addAttribute("signature", stringAttr(context, signature));
  state.addAttribute("arg_count", builder.getI64IntegerAttr(argCount));
  addStringAttr(context, state, "item_kind", itemKind);
  state.addRegion();
  Operation *op = Operation::create(state);
  op->getRegion(0).push_back(new Block());
  return wrap(op);
}

MlirOperation rustMirBlockCreate(MlirLocation location, int64_t index) {
  MLIRContext *context = unwrap(location).getContext();
  Builder builder(context);
  OperationState state(unwrap(location), kRustMIRBlock);
  state.addAttribute("index", builder.getI64IntegerAttr(index));
  state.addRegion();
  Operation *op = Operation::create(state);
  op->getRegion(0).push_back(new Block());
  return wrap(op);
}

MlirBlock rustMirOperationGetBodyBlock(MlirOperation op) {
  Operation *operation = unwrap(op);
  if (operation->getNumRegions() == 0 || operation->getRegion(0).empty())
    return MlirBlock{nullptr};
  return wrap(&operation->getRegion(0).front());
}

MlirOperation rustMirLocalCreate(MlirLocation location, int64_t index,
                                 MlirStringRef name, MlirStringRef role,
                                 MlirType rustType, MlirStringRef mutability) {
  MLIRContext *context = unwrap(location).getContext();
  Builder builder(context);
  OperationState state(unwrap(location), kRustMIRLocal);
  state.addAttribute("index", builder.getI64IntegerAttr(index));
  addStringAttr(context, state, "name", name);
  addStringAttr(context, state, "role", role);
  addStringAttr(context, state, "mutability", mutability);
  state.addAttribute("rust_type", TypeAttr::get(unwrap(rustType)));
  return createOperation(state);
}

MlirOperation rustMirAssignCreate(MlirLocation location, int64_t index,
                                  MlirAttribute payload) {
  MLIRContext *context = unwrap(location).getContext();
  Builder builder(context);
  OperationState state(unwrap(location), kRustMIRAssign);
  state.addAttribute("index", builder.getI64IntegerAttr(index));
  state.addAttribute("mir_kind", builder.getStringAttr("Assign"));
  state.addAttribute("payload", unwrap(payload));
  return createOperation(state);
}

MlirOperation rustMirReturnCreate(MlirLocation location) {
  MLIRContext *context = unwrap(location).getContext();
  Builder builder(context);
  OperationState state(unwrap(location), kRustMIRReturn);
  state.addAttribute("mir_kind", builder.getStringAttr("Return"));
  state.addAttribute("payload", builder.getDictionaryAttr({}));
  return createOperation(state);
}

MlirOperation rustMirPayloadOpCreate(MlirLocation location,
                                     MlirStringRef opName,
                                     MlirStringRef mirKind,
                                     MlirAttribute payload) {
  MLIRContext *context = unwrap(location).getContext();
  Builder builder(context);
  OperationState state(unwrap(location), unwrap(opName));
  addStringAttr(context, state, "mir_kind", mirKind);
  if (payload.ptr)
    state.addAttribute("payload", unwrap(payload));
  return createOperation(state);
}
