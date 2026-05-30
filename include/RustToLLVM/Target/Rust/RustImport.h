//===- RustImport.h - Import Rust MIR NDJSON into MLIR ---------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef RUST_TO_LLVM_TARGET_RUST_RUSTIMPORT_H
#define RUST_TO_LLVM_TARGET_RUST_RUSTIMPORT_H

#include "mlir/IR/OwningOpRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/SourceMgr.h"

namespace mlir {
class MLIRContext;
class Operation;
} // namespace mlir

namespace mlir::rust {

OwningOpRef<Operation *> importRustNdjson(llvm::StringRef input,
                                          MLIRContext *context);
OwningOpRef<Operation *> importRustNdjson(llvm::SourceMgr &sourceMgr,
                                          MLIRContext *context);

} // namespace mlir::rust

#endif // RUST_TO_LLVM_TARGET_RUST_RUSTIMPORT_H
