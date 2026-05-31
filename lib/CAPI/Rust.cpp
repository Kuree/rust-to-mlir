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
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Parser/Parser.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
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
  constexpr llvm::StringLiteral kRepr("repr:");
  span = span.trim();
  size_t reprPos = span.find(kRepr);
  if (reprPos == llvm::StringRef::npos)
    return span;

  llvm::StringRef repr = span.drop_front(reprPos + kRepr.size()).trim();
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

  static const llvm::Regex rangeRegex(
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

  static const llvm::Regex pointRegex("^(.+):([0-9]+):([0-9]+)$");
  if (pointRegex.match(span, &matches)) {
    auto line = parseUnsigned(matches[2]);
    auto column = parseUnsigned(matches[3]);
    if (!line || !column)
      return std::nullopt;
    return ParsedSpan{matches[1].str(), *line, *column, 0, 0, false};
  }

  return std::nullopt;
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

template <typename EnumT, typename AttrT>
void addEnumAttr(MLIRContext *context, OperationState &state, StringRef name,
                 MlirStringRef value,
                 std::optional<EnumT> (*symbolize)(StringRef)) {
  if (!value.data && value.length == 0)
    return;
  if (std::optional<EnumT> symbol = symbolize(unwrap(value)))
    state.addAttribute(name, AttrT::get(context, *symbol));
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

MlirOperation createRegionOperation(OperationState &state) {
  state.addRegion();
  Operation *op = Operation::create(state);
  op->getRegion(0).push_back(new Block());
  return wrap(op);
}

void appendOwnedChild(Operation *parent, MlirOperation child) {
  if (!child.ptr)
    return;
  parent->getRegion(0).front().push_back(unwrap(child));
}

void appendOwnedChildren(Operation *parent, intptr_t numChildren,
                         MlirOperation const *children) {
  for (intptr_t i = 0; i < numChildren; ++i)
    appendOwnedChild(parent, children[i]);
}

StringRef binaryRvalueOpName(MlirStringRef kind) {
  if (unwrap(kind) == "CheckedBinaryOp")
    return "rust.mir.checked_binary_op";
  return "rust.mir.binary_op";
}

StringRef operandDebugOpName(MlirStringRef kind) {
  StringRef kindRef = unwrap(kind);
  if (kindRef == "Constant")
    return "rust.mir.constant";
  if (kindRef == "RuntimeChecks")
    return "rust.mir.runtime_checks";
  return "rust.mir.unsupported_statement";
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

  OwningOpRef<ModuleOp> merged = ModuleOp::create(UnknownLoc::get(&context));
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

// Fallback for the genuinely opaque types the extractor cannot model
// structurally (fn items/pointers, str, closures, trait objects, ...). The
// spelling is rustc's Debug rendering, preserved so passes can still recognise
// e.g. `RigidTy(Str)`. All common types are built structurally in the
// extractor, so this no longer parses spellings.
MlirType rustMirTypeFromRustcPublicString(MlirContext context,
                                          MlirStringRef spelling) {
  return wrap(rustmir::OpaqueType::get(unwrap(context), unwrap(spelling)));
}

MlirType rustMirBoolTypeGet(MlirContext context) {
  return wrap(rustmir::BoolType::get(unwrap(context)));
}

MlirType rustMirIntTypeGet(MlirContext context, MlirStringRef spelling) {
  return wrap(rustmir::IntType::getFromSpelling(unwrap(context), unwrap(spelling)));
}

MlirType rustMirFloatTypeGet(MlirContext context, uint32_t bitWidth) {
  return wrap(rustmir::FloatType::get(unwrap(context), bitWidth));
}

MlirType rustMirCharTypeGet(MlirContext context) {
  return wrap(rustmir::CharType::get(unwrap(context)));
}

MlirType rustMirUnitTypeGet(MlirContext context) {
  return wrap(rustmir::UnitType::get(unwrap(context)));
}

MlirType rustMirNeverTypeGet(MlirContext context) {
  return wrap(rustmir::NeverType::get(unwrap(context)));
}

MlirType rustMirAdtTypeGetIdentified(MlirContext context, MlirStringRef name) {
  return wrap(rustmir::AdtType::getIdentified(unwrap(context), unwrap(name)));
}

void rustMirAdtTypeSetBody(MlirType adt, intptr_t numVariants,
                           MlirType const *variants) {
  auto adtType = cast<rustmir::AdtType>(unwrap(adt));
  SmallVector<Type> variantTypes;
  variantTypes.reserve(numVariants);
  for (intptr_t i = 0; i < numVariants; ++i)
    variantTypes.push_back(unwrap(variants[i]));
  // Idempotent: setting the identical body again succeeds; only a conflicting
  // redefinition fails, which the structural key makes impossible here.
  (void)adtType.setBody(variantTypes);
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

MlirType rustTypedArrayTypeGet(MlirContext context, MlirType elementType,
                               uint64_t length) {
  return wrap(rustmir::TypedArrayType::get(unwrap(context), unwrap(elementType),
                                           length));
}

MlirType rustTypedSliceTypeGet(MlirContext context, MlirType elementType) {
  return wrap(rustmir::TypedSliceType::get(unwrap(context),
                                           unwrap(elementType)));
}

rustmir::RustMutability mutabilityFromString(llvm::StringRef mutability) {
  return rustmir::symbolizeRustMutability(mutability)
      .value_or(rustmir::RustMutability::Shared);
}

MlirType rustTypedRefTypeGet(MlirContext context, MlirStringRef mutability,
                             MlirType pointeeType) {
  return wrap(rustmir::TypedRefType::get(
      unwrap(context), mutabilityFromString(unwrap(mutability)),
      unwrap(pointeeType)));
}

MlirType rustTypedRawPtrTypeGet(MlirContext context, MlirStringRef mutability,
                                MlirType pointeeType) {
  return wrap(rustmir::TypedRawPtrType::get(
      unwrap(context), mutabilityFromString(unwrap(mutability)),
      unwrap(pointeeType)));
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

MlirOperation rustMirProjectionCreate(MlirLocation location, MlirStringRef kind,
                                      MlirStringRef debug) {
  MLIRContext *context = unwrap(location).getContext();
  OperationState state(unwrap(location), "rust.mir.projection");
  addStringAttr(context, state, "mir_kind", kind);
  addStringAttr(context, state, "debug", debug);
  return createOperation(state);
}

MlirOperation rustMirProjectionFieldCreate(MlirLocation location, int64_t index,
                                           MlirStringRef type) {
  MLIRContext *context = unwrap(location).getContext();
  Builder builder(context);
  OperationState state(unwrap(location), "rust.mir.projection_field");
  state.addAttribute("index", builder.getI64IntegerAttr(index));
  addStringAttr(context, state, "ty", type);
  state.addAttribute("mir_kind", builder.getStringAttr("Field"));
  return createOperation(state);
}

MlirOperation rustMirProjectionDerefCreate(MlirLocation location) {
  MLIRContext *context = unwrap(location).getContext();
  Builder builder(context);
  OperationState state(unwrap(location), "rust.mir.projection_deref");
  state.addAttribute("mir_kind", builder.getStringAttr("Deref"));
  return createOperation(state);
}

MlirOperation rustMirProjectionIndexCreate(MlirLocation location,
                                           int64_t local) {
  MLIRContext *context = unwrap(location).getContext();
  Builder builder(context);
  OperationState state(unwrap(location), "rust.mir.projection_index");
  state.addAttribute("local", builder.getI64IntegerAttr(local));
  state.addAttribute("mir_kind", builder.getStringAttr("Index"));
  return createOperation(state);
}

MlirOperation rustMirProjectionConstantIndexCreate(MlirLocation location,
                                                   int64_t offset,
                                                   int64_t minLength,
                                                   bool fromEnd) {
  MLIRContext *context = unwrap(location).getContext();
  Builder builder(context);
  OperationState state(unwrap(location), "rust.mir.projection_constant_index");
  state.addAttribute("offset", builder.getI64IntegerAttr(offset));
  state.addAttribute("min_length", builder.getI64IntegerAttr(minLength));
  state.addAttribute("from_end", builder.getBoolAttr(fromEnd));
  state.addAttribute("mir_kind", builder.getStringAttr("ConstantIndex"));
  return createOperation(state);
}

MlirOperation rustMirProjectionSubsliceCreate(MlirLocation location,
                                              int64_t from, int64_t to,
                                              bool fromEnd) {
  MLIRContext *context = unwrap(location).getContext();
  Builder builder(context);
  OperationState state(unwrap(location), "rust.mir.projection_subslice");
  state.addAttribute("from_index", builder.getI64IntegerAttr(from));
  state.addAttribute("to_index", builder.getI64IntegerAttr(to));
  state.addAttribute("from_end", builder.getBoolAttr(fromEnd));
  state.addAttribute("mir_kind", builder.getStringAttr("Subslice"));
  return createOperation(state);
}

MlirOperation rustMirProjectionDowncastCreate(MlirLocation location,
                                              int64_t variantIndex) {
  MLIRContext *context = unwrap(location).getContext();
  Builder builder(context);
  OperationState state(unwrap(location), "rust.mir.projection_downcast");
  state.addAttribute("variant_index", builder.getI64IntegerAttr(variantIndex));
  state.addAttribute("mir_kind", builder.getStringAttr("Downcast"));
  return createOperation(state);
}

MlirOperation rustMirPlaceCreate(MlirLocation location, int64_t local,
                                 intptr_t numProjections,
                                 MlirOperation const *projections) {
  MLIRContext *context = unwrap(location).getContext();
  Builder builder(context);
  OperationState state(unwrap(location), "rust.mir.place");
  state.addAttribute("local", builder.getI64IntegerAttr(local));
  MlirOperation wrapped = createRegionOperation(state);
  Operation *op = unwrap(wrapped);
  appendOwnedChildren(op, numProjections, projections);
  return wrapped;
}

MlirOperation rustMirCopyCreate(MlirLocation location, MlirOperation place) {
  OperationState state(unwrap(location), "rust.mir.copy");
  MlirOperation wrapped = createRegionOperation(state);
  appendOwnedChild(unwrap(wrapped), place);
  return wrapped;
}

MlirOperation rustMirMoveCreate(MlirLocation location, MlirOperation place) {
  OperationState state(unwrap(location), "rust.mir.move");
  MlirOperation wrapped = createRegionOperation(state);
  appendOwnedChild(unwrap(wrapped), place);
  return wrapped;
}

MlirOperation createConstantOperation(MlirLocation location, Attribute value,
                                      MlirStringRef debug, MlirType type) {
  MLIRContext *context = unwrap(location).getContext();
  Builder builder(context);
  OperationState state(unwrap(location), "rust.mir.constant");
  if (value)
    state.addAttribute("value", value);
  addStringAttr(context, state, "debug", debug);
  if (Type ty = unwrap(type))
    state.addAttribute("ty", TypeAttr::get(ty));
  state.addAttribute("mir_kind", builder.getStringAttr("Constant"));
  return createOperation(state);
}

MlirOperation rustMirConstantI64Create(MlirLocation location, int64_t value,
                                       MlirStringRef debug, MlirType type) {
  Builder builder(unwrap(location).getContext());
  return createConstantOperation(location, builder.getI64IntegerAttr(value),
                                 debug, type);
}

MlirOperation rustMirConstantCreate(MlirLocation location, MlirStringRef debug,
                                    MlirType type) {
  return createConstantOperation(location, Attribute(), debug, type);
}

MlirOperation rustMirConstantStringCreate(MlirLocation location,
                                          MlirStringRef value,
                                          MlirStringRef debug, MlirType type) {
  MLIRContext *context = unwrap(location).getContext();
  Builder builder(context);
  return createConstantOperation(location, builder.getStringAttr(unwrap(value)),
                                 debug, type);
}

MlirOperation rustMirOperandDebugCreate(MlirLocation location,
                                        MlirStringRef kind,
                                        MlirStringRef debug) {
  MLIRContext *context = unwrap(location).getContext();
  OperationState state(unwrap(location), operandDebugOpName(kind));
  addStringAttr(context, state, "mir_kind", kind);
  addStringAttr(context, state, "debug", debug);
  return createOperation(state);
}

MlirOperation rustMirRvalueBinaryOpCreate(MlirLocation location,
                                          MlirStringRef kind, MlirStringRef op,
                                          MlirOperation lhs,
                                          MlirOperation rhs) {
  MLIRContext *context = unwrap(location).getContext();
  OperationState state(unwrap(location), binaryRvalueOpName(kind));
  addStringAttr(context, state, "mir_kind", kind);
  addEnumAttr<rustmir::RustBinaryOpKind, rustmir::RustBinaryOpKindAttr>(
      context, state, "op", op, rustmir::symbolizeRustBinaryOpKind);
  MlirOperation wrapped = createRegionOperation(state);
  Operation *operation = unwrap(wrapped);
  appendOwnedChild(operation, lhs);
  appendOwnedChild(operation, rhs);
  return wrapped;
}

MlirOperation rustMirRvalueUnaryOpCreate(MlirLocation location,
                                         MlirStringRef kind, MlirStringRef op,
                                         MlirOperation operand) {
  MLIRContext *context = unwrap(location).getContext();
  OperationState state(unwrap(location), "rust.mir.unary_op");
  addStringAttr(context, state, "mir_kind", kind);
  addEnumAttr<rustmir::RustUnaryOpKind, rustmir::RustUnaryOpKindAttr>(
      context, state, "op", op, rustmir::symbolizeRustUnaryOpKind);
  MlirOperation wrapped = createRegionOperation(state);
  appendOwnedChild(unwrap(wrapped), operand);
  return wrapped;
}

MlirOperation rustMirRvalueCastCreate(MlirLocation location,
                                      MlirStringRef kind,
                                      MlirStringRef castKind,
                                      MlirOperation operand,
                                      MlirStringRef type,
                                      MlirStringRef debug) {
  MLIRContext *context = unwrap(location).getContext();
  OperationState state(unwrap(location), "rust.mir.cast");
  addStringAttr(context, state, "mir_kind", kind);
  addEnumAttr<rustmir::RustCastKind, rustmir::RustCastKindAttr>(
      context, state, "cast_kind", castKind, rustmir::symbolizeRustCastKind);
  addStringAttr(context, state, "ty", type);
  addStringAttr(context, state, "debug", debug);
  MlirOperation wrapped = createRegionOperation(state);
  appendOwnedChild(unwrap(wrapped), operand);
  return wrapped;
}

MlirOperation rustMirRvalueAggregateCreate(MlirLocation location,
                                           MlirStringRef kind,
                                           MlirStringRef aggregateKind,
                                           int64_t variantIndex,
                                           MlirStringRef discriminant,
                                           intptr_t numOperands,
                                           MlirOperation const *operands) {
  MLIRContext *context = unwrap(location).getContext();
  Builder builder(context);
  OperationState state(unwrap(location), "rust.mir.aggregate");
  addStringAttr(context, state, "mir_kind", kind);
  addEnumAttr<rustmir::RustAggregateKind, rustmir::RustAggregateKindAttr>(
      context, state, "aggregate_kind", aggregateKind,
      rustmir::symbolizeRustAggregateKind);
  if (variantIndex >= 0)
    state.addAttribute("variant_index",
                       builder.getI64IntegerAttr(variantIndex));
  if (discriminant.length > 0)
    addStringAttr(context, state, "discriminant", discriminant);
  MlirOperation wrapped = createRegionOperation(state);
  appendOwnedChildren(unwrap(wrapped), numOperands, operands);
  return wrapped;
}

MlirOperation rustMirRvalueUseCreate(MlirLocation location,
                                     MlirOperation operand) {
  MLIRContext *context = unwrap(location).getContext();
  Builder builder(context);
  OperationState state(unwrap(location), "rust.mir.use");
  state.addAttribute("mir_kind", builder.getStringAttr("Use"));
  MlirOperation wrapped = createRegionOperation(state);
  appendOwnedChild(unwrap(wrapped), operand);
  return wrapped;
}

MlirOperation rustMirRvalueLenCreate(MlirLocation location,
                                     MlirOperation place) {
  MLIRContext *context = unwrap(location).getContext();
  Builder builder(context);
  OperationState state(unwrap(location), "rust.mir.len");
  state.addAttribute("mir_kind", builder.getStringAttr("Len"));
  MlirOperation wrapped = createRegionOperation(state);
  appendOwnedChild(unwrap(wrapped), place);
  return wrapped;
}

MlirOperation rustMirRvalueDiscriminantCreate(MlirLocation location,
                                              MlirOperation place) {
  MLIRContext *context = unwrap(location).getContext();
  Builder builder(context);
  OperationState state(unwrap(location), "rust.mir.discriminant");
  state.addAttribute("mir_kind", builder.getStringAttr("Discriminant"));
  MlirOperation wrapped = createRegionOperation(state);
  appendOwnedChild(unwrap(wrapped), place);
  return wrapped;
}

MlirOperation rustMirRvalueRefCreate(MlirLocation location,
                                     MlirStringRef rustRegion,
                                     MlirStringRef borrowKind,
                                     MlirStringRef mutability,
                                     MlirOperation place, MlirStringRef debug) {
  MLIRContext *context = unwrap(location).getContext();
  Builder builder(context);
  OperationState state(unwrap(location), "rust.mir.ref");
  state.addAttribute("mir_kind", builder.getStringAttr("Ref"));
  addStringAttr(context, state, "rust_region", rustRegion);
  addEnumAttr<rustmir::RustBorrowKind, rustmir::RustBorrowKindAttr>(
      context, state, "borrow_kind", borrowKind,
      rustmir::symbolizeRustBorrowKind);
  addEnumAttr<rustmir::RustMutability, rustmir::RustMutabilityAttr>(
      context, state, "mutability", mutability,
      rustmir::symbolizeRustMutability);
  addStringAttr(context, state, "debug", debug);
  MlirOperation wrapped = createRegionOperation(state);
  appendOwnedChild(unwrap(wrapped), place);
  return wrapped;
}

MlirOperation rustMirRvalueAddressOfCreate(MlirLocation location,
                                           MlirStringRef rawPtrKind,
                                           MlirStringRef mutability,
                                           MlirOperation place,
                                           MlirStringRef debug) {
  MLIRContext *context = unwrap(location).getContext();
  Builder builder(context);
  OperationState state(unwrap(location), "rust.mir.address_of");
  state.addAttribute("mir_kind", builder.getStringAttr("AddressOf"));
  addEnumAttr<rustmir::RustRawPtrKind, rustmir::RustRawPtrKindAttr>(
      context, state, "raw_ptr_kind", rawPtrKind,
      rustmir::symbolizeRustRawPtrKind);
  addEnumAttr<rustmir::RustMutability, rustmir::RustMutabilityAttr>(
      context, state, "mutability", mutability,
      rustmir::symbolizeRustMutability);
  addStringAttr(context, state, "debug", debug);
  MlirOperation wrapped = createRegionOperation(state);
  appendOwnedChild(unwrap(wrapped), place);
  return wrapped;
}

MlirOperation rustMirDebugOpCreate(MlirLocation location, MlirStringRef opName,
                                   MlirStringRef kind, MlirStringRef debug) {
  MLIRContext *context = unwrap(location).getContext();
  OperationState state(unwrap(location), unwrap(opName));
  addStringAttr(context, state, "mir_kind", kind);
  addStringAttr(context, state, "debug", debug);
  return createOperation(state);
}

MlirOperation rustMirGotoCreate(MlirLocation location, int64_t target) {
  MLIRContext *context = unwrap(location).getContext();
  Builder builder(context);
  OperationState state(unwrap(location), "rust.mir.goto");
  state.addAttribute("target", builder.getI64IntegerAttr(target));
  state.addAttribute("mir_kind", builder.getStringAttr("Goto"));
  return createOperation(state);
}

MlirOperation rustMirSwitchIntCreate(MlirLocation location, MlirOperation discr,
                                     MlirAttribute targets,
                                     MlirStringRef debug) {
  MLIRContext *context = unwrap(location).getContext();
  Builder builder(context);
  OperationState state(unwrap(location), "rust.mir.switch_int");
  if (targets.ptr)
    state.addAttribute("targets", unwrap(targets));
  state.addAttribute("mir_kind", builder.getStringAttr("SwitchInt"));
  addStringAttr(context, state, "debug", debug);
  MlirOperation wrapped = createRegionOperation(state);
  appendOwnedChild(unwrap(wrapped), discr);
  return wrapped;
}

MlirOperation rustMirAssertCreate(MlirLocation location, MlirOperation cond,
                                  bool expected, int64_t target,
                                  MlirStringRef debug) {
  MLIRContext *context = unwrap(location).getContext();
  Builder builder(context);
  OperationState state(unwrap(location), "rust.mir.assert");
  state.addAttribute("expected", builder.getBoolAttr(expected));
  state.addAttribute("target", builder.getI64IntegerAttr(target));
  state.addAttribute("mir_kind", builder.getStringAttr("Assert"));
  addStringAttr(context, state, "debug", debug);
  MlirOperation wrapped = createRegionOperation(state);
  appendOwnedChild(unwrap(wrapped), cond);
  return wrapped;
}

MlirOperation rustMirCallCreate(
    MlirLocation location, MlirOperation func, MlirOperation destination,
    bool hasTarget, int64_t target, MlirStringRef unwind, intptr_t numArgs,
    MlirOperation const *args, MlirStringRef debug, MlirStringRef calleeName,
    MlirStringRef calleeDef, MlirStringRef calleeType,
    MlirStringRef calleeGenericArgs, MlirStringRef calleeInputs,
    MlirStringRef calleeOutput, MlirStringRef calleeAbi, bool calleeCVariadic,
    MlirStringRef rangeKind) {
  MLIRContext *context = unwrap(location).getContext();
  Builder builder(context);
  OperationState state(unwrap(location), "rust.mir.call");
  if (hasTarget)
    state.addAttribute("target", builder.getI64IntegerAttr(target));
  state.addAttribute("mir_kind", builder.getStringAttr("Call"));
  addStringAttr(context, state, "debug", debug);
  addEnumAttr<rustmir::RustUnwindAction, rustmir::RustUnwindActionAttr>(
      context, state, "unwind", unwind,
      rustmir::symbolizeRustUnwindAction);
  addStringAttr(context, state, "callee_name", calleeName);
  addStringAttr(context, state, "callee_def", calleeDef);
  addStringAttr(context, state, "callee_type", calleeType);
  addStringAttr(context, state, "callee_generic_args", calleeGenericArgs);
  addStringAttr(context, state, "callee_inputs", calleeInputs);
  addStringAttr(context, state, "callee_output", calleeOutput);
  addEnumAttr<rustmir::RustAbi, rustmir::RustAbiAttr>(
      context, state, "callee_abi", calleeAbi, rustmir::symbolizeRustAbi);
  if (calleeAbi.data || calleeAbi.length != 0)
    state.addAttribute("callee_c_variadic",
                       builder.getBoolAttr(calleeCVariadic));
  addEnumAttr<rustmir::RustRangeKind, rustmir::RustRangeKindAttr>(
      context, state, "range_kind", rangeKind,
      rustmir::symbolizeRustRangeKind);
  MlirOperation wrapped = createRegionOperation(state);
  Operation *op = unwrap(wrapped);
  appendOwnedChild(op, func);
  appendOwnedChild(op, destination);
  appendOwnedChildren(op, numArgs, args);
  return wrapped;
}

MlirOperation rustMirTargetTerminatorCreate(MlirLocation location,
                                            MlirStringRef opName,
                                            MlirStringRef kind, int64_t target,
                                            MlirStringRef debug) {
  MLIRContext *context = unwrap(location).getContext();
  Builder builder(context);
  OperationState state(unwrap(location), unwrap(opName));
  state.addAttribute("target", builder.getI64IntegerAttr(target));
  addStringAttr(context, state, "mir_kind", kind);
  addStringAttr(context, state, "debug", debug);
  return createOperation(state);
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
  addEnumAttr<rustmir::RustLocalRole, rustmir::RustLocalRoleAttr>(
      context, state, "role", role, rustmir::symbolizeRustLocalRole);
  addEnumAttr<rustmir::RustLocalMutability,
              rustmir::RustLocalMutabilityAttr>(
      context, state, "mutability", mutability,
      rustmir::symbolizeRustLocalMutability);
  state.addAttribute("rust_type", TypeAttr::get(unwrap(rustType)));
  return createOperation(state);
}

MlirOperation rustMirAssignCreate(MlirLocation location, int64_t index,
                                  MlirOperation place, MlirOperation rvalue) {
  MLIRContext *context = unwrap(location).getContext();
  Builder builder(context);
  OperationState state(unwrap(location), kRustMIRAssign);
  state.addAttribute("index", builder.getI64IntegerAttr(index));
  state.addAttribute("mir_kind", builder.getStringAttr("Assign"));
  MlirOperation wrapped = createRegionOperation(state);
  Operation *op = unwrap(wrapped);
  appendOwnedChild(op, place);
  appendOwnedChild(op, rvalue);
  return wrapped;
}

MlirOperation rustMirReturnCreate(MlirLocation location) {
  MLIRContext *context = unwrap(location).getContext();
  Builder builder(context);
  OperationState state(unwrap(location), kRustMIRReturn);
  state.addAttribute("mir_kind", builder.getStringAttr("Return"));
  return createOperation(state);
}
