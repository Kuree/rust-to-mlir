//===- RustMIRDialect.cpp - Rust MIR dialect --------------------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/RustMIR/IR/RustMIRDialect.h"
#include "mlir/Dialect/RustMIR/IR/RustOps.h"
#include "mlir/Dialect/RustMIR/IR/RustTypes.h"
#include "mlir/Dialect/RustTyped/IR/RustTypedOps.h"

using namespace mlir;
using namespace mlir::rust::mir;

#include "mlir/Dialect/RustMIR/IR/RustOpsDialect.cpp.inc"

void RustMIRDialect::initialize() {
  addOperations<
#define GET_OP_LIST
#include "mlir/Dialect/RustMIR/IR/RustOps.cpp.inc"
      >();
  addOperations<
#define GET_OP_LIST
#include "mlir/Dialect/RustTyped/IR/RustTypedOps.cpp.inc"
      >();
  registerTypes();
}
