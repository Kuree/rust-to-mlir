//===- RustOps.cpp - Rust MIR dialect operations ----------------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/RustMIR/IR/RustOps.h"

#define GET_OP_CLASSES
#include "mlir/Dialect/RustMIR/IR/RustOps.cpp.inc"

using namespace mlir;
using namespace mlir::rust::mir;

LogicalResult CallOp::verify() {
  Region &body = getBody();
  if (body.empty() || body.front().empty())
    return emitOpError("expected body with callee, destination, and arguments");

  Block &block = body.front();
  if (block.getOperations().size() < 2)
    return emitOpError("expected callee operand and destination place");

  auto it = block.begin();
  if (!isa<ConstantOp, CopyOp, MoveOp>(*it))
    return emitOpError("expected first body operation to be a MIR operand");

  ++it;
  if (!isa<PlaceOp>(*it))
    return emitOpError(
        "expected second body operation to be destination place");

  return success();
}
