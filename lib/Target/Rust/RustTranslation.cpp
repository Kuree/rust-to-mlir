//===- RustTranslation.cpp - Register Rust translations ---------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RustToLLVM/Target/Rust/RustTranslation.h"

#include "mlir/Dialect/RustMIR/IR/RustMIRDialect.h"
#include "RustToLLVM/Target/Rust/RustImport.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/Tools/mlir-translate/Translation.h"

using namespace mlir;
namespace rustmir = mlir::rust::mir;

void mlir::rust::registerRustNdjsonTranslation() {
  TranslateToMLIRRegistration registration(
      "import-rust-ndjson", "Translate rustc_public NDJSON MIR to rust MLIR",
      [](llvm::SourceMgr &sourceMgr,
         MLIRContext *context) -> OwningOpRef<Operation *> {
        return rust::importRustNdjson(sourceMgr, context);
      },
      [](DialectRegistry &registry) { registry.insert<rustmir::RustMIRDialect>(); });
}
