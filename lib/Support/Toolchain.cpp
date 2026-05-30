//===- Toolchain.cpp - RustToLLVM tool setup helpers -----------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RustToLLVM/Support/Toolchain.h"

#include "mlir/Conversion/Passes.h"
#include "mlir/Conversion/RustTypedMemoryToLLVM/RustTypedMemoryToLLVM.h"
#include "mlir/Conversion/RustTypedToArith/RustTypedToArith.h"
#include "mlir/Conversion/RustTypedToControlFlow/RustTypedToControlFlow.h"
#include "mlir/Conversion/RustTypedToFunc/RustTypedToFunc.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/DLTI/DLTI.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Index/IR/IndexDialect.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/RustMIR/IR/RustMIRDialect.h"
#include "mlir/Dialect/RustMIR/Transforms/Passes.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "mlir/Transforms/Passes.h"

using namespace mlir;

void rust_to_llvm::registerRustToLLVMDialects(DialectRegistry &registry) {
  registry.insert<arith::ArithDialect, cf::ControlFlowDialect, DLTIDialect,
                  func::FuncDialect, index::IndexDialect, LLVM::LLVMDialect,
                  rust::mir::RustMIRDialect, ub::UBDialect>();
}

void rust_to_llvm::registerRustToLLVMPasses() {
  registerArithToLLVMConversionPass();
  registerConvertControlFlowToLLVMPass();
  registerConvertFuncToLLVMPass();
  registerConvertIndexToLLVMPass();
  registerReconcileUnrealizedCasts();
  registerUBToLLVMConversionPass();
  registerConvertRustTypedToArithPass();
  registerConvertRustTypedToControlFlowPass();
  registerConvertRustTypedToFuncPass();
  registerConvertRustTypedMemoryToLLVMPass();
  rust::registerRustMIRPasses();
  registerMem2Reg();
  registerSROA();
}

void rust_to_llvm::registerRustToLLVMIRTranslations(DialectRegistry &registry) {
  registerBuiltinDialectTranslation(registry);
  registerLLVMDialectTranslation(registry);
}

void rust_to_llvm::populateRustToLLVMLoweringPipeline(
    OpPassManager &pm, const RustToLLVMLoweringOptions &options) {
  LiftTypedMIRPassOptions liftOptions;
  liftOptions.eraseSourceMIR = options.eraseSourceMIR;

  pm.addPass(rust::createLiftTypedMIRPass(liftOptions));
  pm.addPass(createSROA());
  pm.addPass(createConvertRustTypedToArithPass());
  pm.addPass(createConvertRustTypedToFuncPass());
  pm.addPass(createConvertRustTypedToControlFlowPass());
  pm.addPass(createMem2Reg());
  pm.addPass(createConvertRustTypedMemoryToLLVMPass());
  pm.addPass(createArithToLLVMConversionPass());
  pm.addPass(createConvertControlFlowToLLVMPass());
  pm.addPass(createConvertFuncToLLVMPass());
  pm.addPass(createConvertIndexToLLVMPass());
  pm.addPass(createUBToLLVMConversionPass());
  pm.addPass(createReconcileUnrealizedCastsPass());
}

LogicalResult
rust_to_llvm::lowerRustToLLVM(Operation *op,
                              const RustToLLVMLoweringOptions &options) {
  PassManager pm(op->getContext());
  populateRustToLLVMLoweringPipeline(pm, options);
  return pm.run(op);
}
