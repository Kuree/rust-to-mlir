//===- rust-opt.cpp - Rust MLIR optimizer driver ---------------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Conversion/RustTypedMemoryToLLVM/RustTypedMemoryToLLVM.h"
#include "mlir/Conversion/RustTypedToArith/RustTypedToArith.h"
#include "mlir/Conversion/RustTypedToControlFlow/RustTypedToControlFlow.h"
#include "mlir/Conversion/RustTypedToFunc/RustTypedToFunc.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/DLTI/DLTI.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/RustMIR/IR/RustMIRDialect.h"
#include "mlir/Dialect/RustMIR/Transforms/Passes.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/Tools/mlir-opt/MlirOptMain.h"
#include "mlir/Transforms/Passes.h"

int main(int argc, char **argv) {
  mlir::DialectRegistry registry;
  registry.insert<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
                  mlir::DLTIDialect, mlir::func::FuncDialect,
                  mlir::LLVM::LLVMDialect, mlir::rust::mir::RustMIRDialect,
                  mlir::ub::UBDialect>();
  mlir::registerConvertRustTypedToArithPass();
  mlir::registerConvertRustTypedToControlFlowPass();
  mlir::registerConvertRustTypedToFuncPass();
  mlir::registerConvertRustTypedMemoryToLLVMPass();
  mlir::rust::registerRustMIRPasses();
  mlir::registerMem2Reg();
  mlir::registerSROA();
  return mlir::asMainReturnCode(
      mlir::MlirOptMain(argc, argv, "Rust MLIR optimizer driver\n", registry));
}
