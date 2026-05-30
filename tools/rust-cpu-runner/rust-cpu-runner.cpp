//===- rust-cpu-runner.cpp - Rust MLIR CPU runner -------------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RustToLLVM/Support/Toolchain.h"
#include "mlir/ExecutionEngine/ExecutionEngine.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Pass/PassManager.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/Mangling.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorSymbolDef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

namespace cl = llvm::cl;

extern "C" void __rust_to_llvm_print_i32(std::int32_t value) {
  std::printf("%" PRId32 "\n", value);
  std::fflush(stdout);
}

namespace {
enum class EntryPointResult { Void, I32, I64, F32 };

cl::opt<std::string> inputFilename(cl::Positional, cl::desc("<input file>"),
                                   cl::Required);
cl::opt<std::string> entryPoint("e", cl::desc("Function to run"),
                                cl::init("main"));
cl::opt<EntryPointResult> entryPointResult(
    "entry-point-result", cl::desc("Textual description of the function type"),
    cl::values(clEnumValN(EntryPointResult::Void, "void", "void result"),
               clEnumValN(EntryPointResult::I32, "i32", "i32 result"),
               clEnumValN(EntryPointResult::I64, "i64", "i64 result"),
               clEnumValN(EntryPointResult::F32, "f32", "f32 result")),
    cl::init(EntryPointResult::Void));
cl::list<std::string> sharedLibs("shared-libs",
                                 cl::desc("Libraries to link dynamically"),
                                 cl::CommaSeparated);

llvm::orc::SymbolMap getRuntimeSymbols(llvm::orc::MangleAndInterner mangle) {
  llvm::orc::SymbolMap symbols;
  symbols[mangle("__rust_to_llvm_print_i32")] =
      llvm::orc::ExecutorSymbolDef::fromPtr(&__rust_to_llvm_print_i32,
                                            llvm::JITSymbolFlags::Exported);
  return symbols;
}

mlir::LogicalResult lowerRustInput(mlir::Operation *op) {
  mlir::PassManager pm(op->getContext());
  rust_to_llvm::populateRustToLLVMLoweringPipeline(pm);
  if (failed(mlir::applyPassManagerCLOptions(pm)))
    return mlir::failure();
  return pm.run(op);
}

template <typename T>
int invokeAndPrintResult(mlir::ExecutionEngine &engine, llvm::StringRef name) {
  T result{};
  llvm::SmallVector<void *> args{&result};
  if (llvm::Error error = engine.invokePacked(name, args)) {
    llvm::logAllUnhandledErrors(std::move(error), llvm::errs(), "Error: ");
    return 1;
  }
  llvm::outs() << result << '\n';
  return 0;
}

int invokeEntryPoint(mlir::ExecutionEngine &engine) {
  switch (entryPointResult) {
  case EntryPointResult::Void:
    if (llvm::Error error = engine.invokePacked(entryPoint)) {
      llvm::logAllUnhandledErrors(std::move(error), llvm::errs(), "Error: ");
      return 1;
    }
    return 0;
  case EntryPointResult::I32:
    return invokeAndPrintResult<std::int32_t>(engine, entryPoint);
  case EntryPointResult::I64:
    return invokeAndPrintResult<std::int64_t>(engine, entryPoint);
  case EntryPointResult::F32:
    return invokeAndPrintResult<float>(engine, entryPoint);
  }
  llvm_unreachable("unknown entry point result kind");
}
} // namespace

int main(int argc, char **argv) {
  llvm::InitLLVM y(argc, argv);
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmParser();
  llvm::InitializeNativeTargetAsmPrinter();

  mlir::registerMLIRContextCLOptions();
  mlir::registerPassManagerCLOptions();
  rust_to_llvm::registerRustToLLVMPasses();
  cl::ParseCommandLineOptions(argc, argv, "Rust MLIR CPU runner\n");

  mlir::DialectRegistry registry;
  rust_to_llvm::registerRustToLLVMDialects(registry);
  rust_to_llvm::registerRustToLLVMIRTranslations(registry);

  mlir::MLIRContext context(registry);
  llvm::SourceMgr sourceMgr;
  mlir::SourceMgrDiagnosticHandler sourceMgrHandler(sourceMgr, &context);
  mlir::ParserConfig parserConfig(&context);
  mlir::OwningOpRef<mlir::ModuleOp> module =
      mlir::parseSourceFile<mlir::ModuleOp>(inputFilename, sourceMgr,
                                            parserConfig);
  if (!module)
    return 1;

  if (failed(lowerRustInput(*module)))
    return 1;

  llvm::SmallVector<llvm::StringRef> sharedLibRefs;
  sharedLibRefs.reserve(sharedLibs.size());
  for (const std::string &lib : sharedLibs)
    sharedLibRefs.push_back(lib);

  mlir::ExecutionEngineOptions engineOptions;
  engineOptions.sharedLibPaths = sharedLibRefs;
  llvm::Expected<std::unique_ptr<mlir::ExecutionEngine>> expectedEngine =
      mlir::ExecutionEngine::create(*module, engineOptions);
  if (!expectedEngine) {
    llvm::logAllUnhandledErrors(expectedEngine.takeError(), llvm::errs(),
                                "Error: ");
    return 1;
  }

  std::unique_ptr<mlir::ExecutionEngine> engine = std::move(*expectedEngine);
  engine->registerSymbols(getRuntimeSymbols);
  return invokeEntryPoint(*engine);
}
