//===- RustTypedMemoryToLLVM.h - Rust typed memory to LLVM -----*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_CONVERSION_RUSTTYPEDMEMORYTOLLVM_RUSTTYPEDMEMORYTOLLVM_H
#define MLIR_CONVERSION_RUSTTYPEDMEMORYTOLLVM_RUSTTYPEDMEMORYTOLLVM_H

#include "mlir/Pass/Pass.h"

#include <memory>

namespace mlir {
class Pass;

std::unique_ptr<Pass> createConvertRustTypedMemoryToLLVMPass();

#define GEN_PASS_DECL_CONVERTRUSTTYPEDMEMORYTOLLVMPASS
#define GEN_PASS_REGISTRATION
#include "mlir/Conversion/RustTypedMemoryToLLVM/RustTypedMemoryToLLVMPasses.h.inc"
} // namespace mlir

#endif // MLIR_CONVERSION_RUSTTYPEDMEMORYTOLLVM_RUSTTYPEDMEMORYTOLLVM_H
