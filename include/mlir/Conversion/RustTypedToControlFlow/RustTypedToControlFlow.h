//===- RustTypedToControlFlow.h - Rust typed to cf conversion ---*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_CONVERSION_RUSTTYPEDTOCONTROLFLOW_RUSTTYPEDTOCONTROLFLOW_H
#define MLIR_CONVERSION_RUSTTYPEDTOCONTROLFLOW_RUSTTYPEDTOCONTROLFLOW_H

#include "mlir/Pass/Pass.h"

#include <memory>

namespace mlir {

std::unique_ptr<Pass> createConvertRustTypedToControlFlowPass();

#define GEN_PASS_DECL_CONVERTRUSTTYPEDTOCONTROLFLOWPASS
#define GEN_PASS_REGISTRATION
#include "mlir/Conversion/RustTypedToControlFlow/RustTypedToControlFlowPasses.h.inc"

} // namespace mlir

#endif // MLIR_CONVERSION_RUSTTYPEDTOCONTROLFLOW_RUSTTYPEDTOCONTROLFLOW_H
