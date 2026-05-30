//===- rust-translate.cpp - Rust MIR to MLIR translator ---------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/RustMIR/IR/RustMIRDialect.h"
#include "RustToLLVM/Target/Rust/RustImport.h"
#include "mlir/IR/AsmState.h"
#include "mlir/IR/MLIRContext.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"

using namespace mlir;
namespace rustmir = mlir::rust::mir;

static llvm::cl::opt<std::string> fromNdjson(
    "from-ndjson", llvm::cl::desc("Import an NDJSON stream emitted by rust-mir-extract"),
    llvm::cl::value_desc("path"));

static llvm::cl::opt<std::string> crateRoot(
    "rs", llvm::cl::desc("Run rust-mir-extract on a Rust crate root"),
    llvm::cl::value_desc("path"));

static llvm::cl::opt<std::string> cargoPackage(
    "cargo", llvm::cl::desc("Run rust-mir-extract in Cargo package mode"),
    llvm::cl::value_desc("package-dir"));

static llvm::cl::opt<std::string> extractorPath(
    "rust-mir-extract",
    llvm::cl::desc("Path to rust-mir-extract; defaults to PATH lookup"),
    llvm::cl::value_desc("path"));

static llvm::cl::opt<std::string> outputFilename(
    "o", llvm::cl::desc("Output filename; defaults to stdout"),
    llvm::cl::value_desc("path"));

static llvm::cl::list<std::string> passthroughArgs(
    llvm::cl::Positional, llvm::cl::ZeroOrMore, llvm::cl::ConsumeAfter,
    llvm::cl::desc("<arguments passed to rustc/cargo after -->"));

static int countInputModes() {
  return (!fromNdjson.empty() ? 1 : 0) + (!crateRoot.empty() ? 1 : 0) +
         (!cargoPackage.empty() ? 1 : 0);
}

static std::string lookupExtractor() {
  if (!extractorPath.empty())
    return extractorPath;
  if (auto path = llvm::sys::findProgramByName("rust-mir-extract"))
    return *path;
  return "rust-mir-extract";
}

static llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
runExtractorToBuffer(bool cargoMode) {
  llvm::SmallString<128> tempPath;
  if (std::error_code ec =
          llvm::sys::fs::createTemporaryFile("rust-mir", "ndjson", tempPath))
    return ec;
  llvm::FileRemover cleanup(tempPath);

  std::string program = lookupExtractor();
  std::vector<std::string> storage;
  storage.push_back(program);
  if (cargoMode) {
    storage.push_back("--cargo");
    storage.push_back(cargoPackage);
  } else {
    storage.push_back("--crate-root");
    storage.push_back(crateRoot);
  }
  storage.push_back("--emit-ndjson");
  storage.push_back("-o");
  storage.push_back(tempPath.str().str());
  if (!passthroughArgs.empty()) {
    storage.push_back("--");
    for (const std::string &arg : passthroughArgs)
      storage.push_back(arg);
  }

  std::vector<llvm::StringRef> args;
  args.reserve(storage.size());
  for (const std::string &arg : storage)
    args.push_back(arg);

  int rc = llvm::sys::ExecuteAndWait(program, args);
  if (rc != 0) {
    llvm::WithColor::error()
        << "rust-mir-extract failed with exit code " << rc << "\n";
    return std::make_error_code(std::errc::executable_format_error);
  }

  return llvm::MemoryBuffer::getFile(tempPath);
}

int main(int argc, char **argv) {
  llvm::InitLLVM y(argc, argv);
  registerAsmPrinterCLOptions();
  llvm::cl::ParseCommandLineOptions(argc, argv,
                                    "Rust MIR to MLIR translation tool\n");

  if (countInputModes() != 1) {
    llvm::WithColor::error()
        << "choose exactly one of --from-ndjson, --rs, or --cargo\n";
    return 1;
  }

  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> bufferOrErr =
      !fromNdjson.empty()
          ? llvm::MemoryBuffer::getFileOrSTDIN(fromNdjson)
          : runExtractorToBuffer(!cargoPackage.empty());
  if (!bufferOrErr) {
    llvm::WithColor::error() << "failed to read input: "
                             << bufferOrErr.getError().message() << "\n";
    return 1;
  }

  MLIRContext context;
  context.loadDialect<rustmir::RustMIRDialect>();
  OwningOpRef<Operation *> module =
      rust::importRustNdjson((*bufferOrErr)->getBuffer(), &context);
  if (!module)
    return 1;

  std::unique_ptr<llvm::ToolOutputFile> outputFile;
  llvm::raw_ostream *os = &llvm::outs();
  if (!outputFilename.empty()) {
    std::error_code ec;
    outputFile = std::make_unique<llvm::ToolOutputFile>(
        outputFilename, ec, llvm::sys::fs::OF_Text);
    if (ec) {
      llvm::WithColor::error() << "failed to open output: " << ec.message()
                               << "\n";
      return 1;
    }
    os = &outputFile->os();
  }

  module.get()->print(*os);
  *os << "\n";
  if (outputFile)
    outputFile->keep();
  return 0;
}
