//===- Rust.h - C API for the Rust dialect ----------------------*- C -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_C_DIALECT_RUST_H
#define MLIR_C_DIALECT_RUST_H

#include "mlir-c/IR.h"
#include "mlir-c/Support.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

MLIR_DECLARE_CAPI_DIALECT_REGISTRATION(RustMIR, rust);

MLIR_CAPI_EXPORTED MlirContext rustMlirContextCreate(void);
MLIR_CAPI_EXPORTED void rustMlirContextDestroy(MlirContext context);
MLIR_CAPI_EXPORTED MlirLocation rustMlirLocationUnknownGet(MlirContext context);
MLIR_CAPI_EXPORTED MlirLocation
rustMlirLocationFromRustSpan(MlirContext context, MlirStringRef span);

MLIR_CAPI_EXPORTED MlirModule rustMlirModuleCreate(MlirLocation location);
MLIR_CAPI_EXPORTED MlirModule rustMlirParseSourceFile(MlirContext context,
                                                       MlirStringRef path);
MLIR_CAPI_EXPORTED void rustMlirModuleDestroy(MlirModule module);
MLIR_CAPI_EXPORTED MlirOperation rustMlirModuleGetOperation(MlirModule module);
MLIR_CAPI_EXPORTED MlirBlock rustMlirModuleGetBody(MlirModule module);
MLIR_CAPI_EXPORTED void rustMlirBlockAppendOwnedOperation(MlirBlock block,
                                                          MlirOperation op);
MLIR_CAPI_EXPORTED bool rustMlirOperationVerify(MlirOperation op);
MLIR_CAPI_EXPORTED bool rustMlirWriteBytecodeToFile(MlirOperation op,
                                                    MlirStringRef path);
MLIR_CAPI_EXPORTED bool rustMlirWriteTextToFile(MlirOperation op,
                                                MlirStringRef path);
MLIR_CAPI_EXPORTED bool rustMlirMergeTextModulesToFile(
    intptr_t numInputs, MlirStringRef const *inputPaths, MlirStringRef path,
    bool emitBytecode);
MLIR_CAPI_EXPORTED bool rustMlirLowerRustToLLVM(MlirOperation op,
                                                bool eraseSourceMIR);
MLIR_CAPI_EXPORTED bool rustMlirRunPassPipeline(MlirOperation op,
                                                MlirStringRef pipeline);
// Executes a lowered LLVM dialect entry point. A void entry returns status 0;
// an i32 entry returns that value as the status. Results are not printed.
MLIR_CAPI_EXPORTED int rustMlirExecuteMain(
    MlirModule module, MlirStringRef entryPoint, intptr_t numSharedLibs,
    MlirStringRef const *sharedLibs);

MLIR_CAPI_EXPORTED void rustMirModuleSetTarget(MlirModule module,
                                               int64_t pointerWidth,
                                               MlirStringRef endian);

MLIR_CAPI_EXPORTED MlirType
rustMirTypeFromRustcPublicString(MlirContext context, MlirStringRef spelling);
MLIR_CAPI_EXPORTED MlirType rustMirBoolTypeGet(MlirContext context);
MLIR_CAPI_EXPORTED MlirType rustMirCharTypeGet(MlirContext context);
MLIR_CAPI_EXPORTED MlirType rustMirIntTypeGet(MlirContext context,
                                              MlirStringRef spelling);
MLIR_CAPI_EXPORTED MlirType rustMirFloatTypeGet(MlirContext context,
                                                uint32_t bitWidth);
MLIR_CAPI_EXPORTED MlirType rustMirUnitTypeGet(MlirContext context);
MLIR_CAPI_EXPORTED MlirType rustMirNeverTypeGet(MlirContext context);
MLIR_CAPI_EXPORTED MlirType rustMirAdtTypeGetIdentified(MlirContext context,
                                                        MlirStringRef name);
MLIR_CAPI_EXPORTED void rustMirAdtTypeSetBody(MlirType adt,
                                              intptr_t numVariants,
                                              MlirType const *variants);
MLIR_CAPI_EXPORTED MlirType rustMirOpaqueTypeGet(MlirContext context,
                                                  MlirStringRef spelling);
MLIR_CAPI_EXPORTED MlirType rustMlirFunctionTypeGet(
    MlirContext context, intptr_t numInputs, MlirType const *inputs,
    intptr_t numResults, MlirType const *results);
MLIR_CAPI_EXPORTED MlirType rustTypedSlotTypeGet(MlirContext context,
                                                  MlirType elementType);
MLIR_CAPI_EXPORTED MlirType rustTypedTupleTypeGet(MlirContext context,
                                                  intptr_t numElementTypes,
                                                  MlirType const *elementTypes);
MLIR_CAPI_EXPORTED MlirType rustTypedArrayTypeGet(MlirContext context,
                                                  MlirType elementType,
                                                  uint64_t length);
MLIR_CAPI_EXPORTED MlirType rustTypedSliceTypeGet(MlirContext context,
                                                  MlirType elementType);
MLIR_CAPI_EXPORTED MlirType rustTypedRefTypeGet(MlirContext context,
                                                MlirStringRef mutability,
                                                MlirType pointeeType);
MLIR_CAPI_EXPORTED MlirType rustTypedRawPtrTypeGet(MlirContext context,
                                                   MlirStringRef mutability,
                                                   MlirType pointeeType);

MLIR_CAPI_EXPORTED MlirAttribute rustMirSwitchTargetsAttrGet(
    MlirContext context, int64_t otherwise, intptr_t numBranches,
    int64_t const *values, int64_t const *targets);

MLIR_CAPI_EXPORTED MlirOperation rustMirProjectionCreate(MlirLocation location,
                                                         MlirStringRef kind,
                                                         MlirStringRef debug);
MLIR_CAPI_EXPORTED MlirOperation
rustMirProjectionDerefCreate(MlirLocation location);
MLIR_CAPI_EXPORTED MlirOperation rustMirProjectionFieldCreate(
    MlirLocation location, int64_t index, MlirStringRef type);
MLIR_CAPI_EXPORTED MlirOperation
rustMirProjectionIndexCreate(MlirLocation location, int64_t local);
MLIR_CAPI_EXPORTED MlirOperation rustMirProjectionConstantIndexCreate(
    MlirLocation location, int64_t offset, int64_t minLength, bool fromEnd);
MLIR_CAPI_EXPORTED MlirOperation rustMirProjectionSubsliceCreate(
    MlirLocation location, int64_t from, int64_t to, bool fromEnd);
MLIR_CAPI_EXPORTED MlirOperation
rustMirProjectionDowncastCreate(MlirLocation location, int64_t variantIndex);
MLIR_CAPI_EXPORTED MlirOperation
rustMirPlaceCreate(MlirLocation location, int64_t local,
                   intptr_t numProjections,
                   MlirOperation const *projections);
MLIR_CAPI_EXPORTED MlirOperation rustMirCopyCreate(MlirLocation location,
                                                   MlirOperation place);
MLIR_CAPI_EXPORTED MlirOperation rustMirMoveCreate(MlirLocation location,
                                                   MlirOperation place);
MLIR_CAPI_EXPORTED MlirOperation rustMirConstantI64Create(
    MlirLocation location, int64_t value, MlirStringRef debug, MlirType type);
MLIR_CAPI_EXPORTED MlirOperation rustMirConstantCreate(MlirLocation location,
                                                       MlirStringRef debug,
                                                       MlirType type);
MLIR_CAPI_EXPORTED MlirOperation rustMirConstantStringCreate(
    MlirLocation location, MlirStringRef value, MlirStringRef debug,
    MlirType type);
MLIR_CAPI_EXPORTED MlirOperation rustMirOperandDebugCreate(
    MlirLocation location, MlirStringRef kind, MlirStringRef debug);
MLIR_CAPI_EXPORTED MlirOperation rustMirRvalueBinaryOpCreate(
    MlirLocation location, MlirStringRef kind, MlirStringRef op,
    MlirOperation lhs, MlirOperation rhs);
MLIR_CAPI_EXPORTED MlirOperation rustMirRvalueUnaryOpCreate(
    MlirLocation location, MlirStringRef kind, MlirStringRef op,
    MlirOperation operand);
MLIR_CAPI_EXPORTED MlirOperation rustMirRvalueCastCreate(
    MlirLocation location, MlirStringRef kind, MlirStringRef castKind,
    MlirOperation operand, MlirStringRef type, MlirStringRef debug);
MLIR_CAPI_EXPORTED MlirOperation rustMirRvalueAggregateCreate(
    MlirLocation location, MlirStringRef kind, MlirStringRef aggregateKind,
    int64_t variantIndex, MlirStringRef discriminant, intptr_t numOperands,
    MlirOperation const *operands);
MLIR_CAPI_EXPORTED MlirOperation rustMirRvalueCopyForDerefCreate(
    MlirLocation location, MlirOperation place, MlirStringRef debug);
MLIR_CAPI_EXPORTED MlirOperation rustMirRvalueRepeatCreate(
    MlirLocation location, MlirOperation operand, int64_t count,
    MlirStringRef debug);
MLIR_CAPI_EXPORTED MlirOperation rustMirRvalueUseCreate(MlirLocation location,
                                                        MlirOperation operand);
MLIR_CAPI_EXPORTED MlirOperation rustMirRvalueLenCreate(MlirLocation location,
                                                        MlirOperation place);
MLIR_CAPI_EXPORTED MlirOperation
rustMirRvalueDiscriminantCreate(MlirLocation location, MlirOperation place);
MLIR_CAPI_EXPORTED MlirOperation rustMirRvalueRefCreate(
    MlirLocation location, MlirStringRef rustRegion, MlirStringRef borrowKind,
    MlirStringRef mutability, MlirOperation place, MlirStringRef debug);
MLIR_CAPI_EXPORTED MlirOperation rustMirRvalueAddressOfCreate(
    MlirLocation location, MlirStringRef rawPtrKind, MlirStringRef mutability,
    MlirOperation place, MlirStringRef debug);
MLIR_CAPI_EXPORTED MlirOperation rustMirDebugOpCreate(
    MlirLocation location, MlirStringRef opName, MlirStringRef kind,
    MlirStringRef debug);
MLIR_CAPI_EXPORTED MlirOperation rustMirGotoCreate(MlirLocation location,
                                                   int64_t target);
MLIR_CAPI_EXPORTED MlirOperation rustMirSwitchIntCreate(
    MlirLocation location, MlirOperation discr, MlirAttribute targets,
    MlirStringRef debug);
MLIR_CAPI_EXPORTED MlirOperation rustMirAssertCreate(
    MlirLocation location, MlirOperation cond, bool expected, int64_t target,
    MlirStringRef debug);
MLIR_CAPI_EXPORTED MlirOperation rustMirDropCreate(
    MlirLocation location, MlirOperation place, int64_t target,
    MlirStringRef unwind, MlirStringRef debug,
    MlirStringRef calleeBridgeSymbol);
MLIR_CAPI_EXPORTED MlirOperation rustMirCallCreate(
    MlirLocation location, MlirOperation func, MlirOperation destination,
    bool hasTarget, int64_t target, MlirStringRef unwind, intptr_t numArgs,
    MlirOperation const *args, MlirStringRef debug, MlirStringRef calleeName,
    MlirStringRef calleeDef, MlirStringRef calleeType,
    MlirStringRef calleeGenericArgs, MlirStringRef calleeInputs,
    MlirStringRef calleeOutput, MlirStringRef calleeAbi, bool calleeCVariadic,
    MlirStringRef calleeBridgeSymbol, MlirStringRef rangeKind);
MLIR_CAPI_EXPORTED MlirOperation rustMirTargetTerminatorCreate(
    MlirLocation location, MlirStringRef opName, MlirStringRef kind,
    int64_t target, MlirStringRef debug);

MLIR_CAPI_EXPORTED MlirOperation rustMirFuncCreate(
    MlirLocation location, MlirStringRef symName, MlirStringRef rustName,
    MlirStringRef signature, int64_t argCount, MlirStringRef itemKind);
MLIR_CAPI_EXPORTED MlirOperation rustMirBlockCreate(MlirLocation location,
                                                    int64_t index);
MLIR_CAPI_EXPORTED MlirBlock rustMirOperationGetBodyBlock(MlirOperation op);
MLIR_CAPI_EXPORTED MlirOperation rustMirLocalCreate(
    MlirLocation location, int64_t index, MlirStringRef name,
    MlirStringRef role, MlirType rustType, MlirStringRef mutability);
MLIR_CAPI_EXPORTED MlirOperation rustMirAssignCreate(MlirLocation location,
                                                     int64_t index,
                                                     MlirOperation place,
                                                     MlirOperation rvalue);
MLIR_CAPI_EXPORTED MlirOperation rustMirReturnCreate(MlirLocation location);

#ifdef __cplusplus
}
#endif

#endif // MLIR_C_DIALECT_RUST_H
