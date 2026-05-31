//===- rust-cpu-runner.cpp - Rust MLIR CPU runner -------------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RustToLLVM/Support/Toolchain.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "mlir/ExecutionEngine/ExecutionEngine.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Pass/PassManager.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace cl = llvm::cl;

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
cl::opt<std::string> rustcPath(
    "rustc-path",
    cl::desc("Path to rustc for Rust std shared library discovery"),
    cl::init(""));
cl::opt<std::string> rustTargetLibdir(
    "rust-target-libdir",
    cl::desc("Rust target libdir containing libstd shared library"),
    cl::init(""));
cl::opt<std::string> runtimeLibrary(
    "runtime-library",
    cl::desc("Path to the RustToLLVM runtime shim shared library"),
    cl::init(""));

llvm::StringRef getRuntimeLibraryFilename() {
#if defined(_WIN32)
  return "rust_to_llvm_runtime.dll";
#elif defined(__APPLE__)
  return "librust_to_llvm_runtime.dylib";
#else
  return "librust_to_llvm_runtime.so";
#endif
}

llvm::StringRef getRuntimeLibraryEnvVar() {
  return "RUST_TO_LLVM_RUNTIME_LIBRARY";
}

bool isRustStdSharedLibrary(llvm::StringRef filename) {
#if defined(_WIN32)
  return filename.starts_with("std-") && filename.ends_with(".dll");
#elif defined(__APPLE__)
  return filename.starts_with("libstd-") && filename.ends_with(".dylib");
#else
  return filename.starts_with("libstd-") && filename.ends_with(".so");
#endif
}

std::optional<std::string> findRustcInPath() {
  llvm::ErrorOr<std::string> rustc = llvm::sys::findProgramByName("rustc");
  if (!rustc)
    return std::nullopt;
  return *rustc;
}

std::optional<std::string> resolveRustcPath(std::string &diagnostic) {
  if (!rustcPath.empty()) {
    llvm::ErrorOr<std::string> rustc = llvm::sys::findProgramByName(rustcPath);
    if (rustc)
      return *rustc;

    diagnostic = "failed to find rustc from --rustc-path='" + rustcPath +
                 "': " + rustc.getError().message();
    return std::nullopt;
  }

  if (std::optional<std::string> rustc = findRustcInPath())
    return rustc;

  diagnostic = "rustc was not found in PATH";
  return std::nullopt;
}

std::optional<std::string> getRustTargetLibdirFromRustc(llvm::StringRef rustc,
                                                        std::string &diagnostic) {
  llvm::SmallString<128> outputPath;
  if (std::error_code ec = llvm::sys::fs::createTemporaryFile(
          "rust-to-llvm-target-libdir", "txt", outputPath)) {
    diagnostic = "failed to create temporary file for rustc output: " +
                 ec.message();
    return std::nullopt;
  }
  auto cleanupOutput =
      llvm::make_scope_exit([&] { llvm::sys::fs::remove(outputPath); });

  std::array<llvm::StringRef, 3> args = {rustc, "--print", "target-libdir"};
  std::array<std::optional<llvm::StringRef>, 3> redirects = {
      std::nullopt, llvm::StringRef(outputPath), std::nullopt};

  std::string errorMessage;
  bool executionFailed = false;
  int result = llvm::sys::ExecuteAndWait(rustc, args, std::nullopt, redirects,
                                         /*SecondsToWait=*/0,
                                         /*MemoryLimit=*/0, &errorMessage,
                                         &executionFailed);
  if (executionFailed || result != 0) {
    diagnostic = "failed to run '" + rustc.str() +
                 " --print target-libdir'";
    if (!errorMessage.empty())
      diagnostic += ": " + errorMessage;
    return std::nullopt;
  }

  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> buffer =
      llvm::MemoryBuffer::getFile(outputPath);
  if (!buffer) {
    diagnostic = "failed to read rustc target libdir output: " +
                 buffer.getError().message();
    return std::nullopt;
  }

  std::string libdir = (*buffer)->getBuffer().trim().str();
  if (libdir.empty()) {
    diagnostic = "rustc returned an empty target libdir";
    return std::nullopt;
  }
  return libdir;
}

std::optional<std::string> findRustStdSharedLibrary(llvm::StringRef libdir,
                                                    std::string &diagnostic) {
  std::error_code ec;
  llvm::sys::fs::directory_iterator it(libdir, ec);
  if (ec) {
    diagnostic = "failed to scan Rust target libdir '" + libdir.str() +
                 "': " + ec.message();
    return std::nullopt;
  }

  std::vector<std::string> matches;
  llvm::sys::fs::directory_iterator end;
  for (; it != end; it.increment(ec)) {
    if (ec) {
      diagnostic = "failed while scanning Rust target libdir '" + libdir.str() +
                   "': " + ec.message();
      return std::nullopt;
    }

    llvm::StringRef filename = llvm::sys::path::filename(it->path());
    if (isRustStdSharedLibrary(filename))
      matches.push_back(it->path());
  }

  if (matches.empty()) {
    diagnostic =
        "could not find Rust std shared library in '" + libdir.str() + "'";
    return std::nullopt;
  }

  std::sort(matches.begin(), matches.end());
  return matches.front();
}

bool addDiscoveredRustStdSharedLibrary(std::vector<std::string> &libraries) {
  std::string diagnostic;
  std::optional<std::string> libdir;
  bool explicitRustPath = !rustTargetLibdir.empty() || !rustcPath.empty();

  if (!rustTargetLibdir.empty()) {
    libdir = rustTargetLibdir;
  } else if (std::optional<std::string> rustc = resolveRustcPath(diagnostic)) {
    libdir = getRustTargetLibdirFromRustc(*rustc, diagnostic);
  }

  if (!libdir) {
    if (explicitRustPath)
      llvm::errs() << "error: " << diagnostic << "\n";
    return !explicitRustPath;
  }

  std::optional<std::string> libstd =
      findRustStdSharedLibrary(*libdir, diagnostic);
  if (!libstd) {
    if (explicitRustPath)
      llvm::errs() << "error: " << diagnostic << "\n";
    return !explicitRustPath;
  }

  if (std::find(libraries.begin(), libraries.end(), *libstd) == libraries.end())
    libraries.insert(libraries.begin(), *libstd);
  return true;
}

void addUniquePath(std::vector<std::string> &paths, llvm::StringRef path) {
  if (path.empty())
    return;
  if (std::find(paths.begin(), paths.end(), path) == paths.end())
    paths.push_back(path.str());
}

void addExecutableRelativeRuntimeCandidates(const char *argv0,
                                            std::vector<std::string> &paths) {
  std::string executable =
      llvm::sys::fs::getMainExecutable(argv0, reinterpret_cast<void *>(
                                                  &getRuntimeLibraryFilename));
  if (executable.empty())
    return;

  llvm::SmallString<256> executableDir(executable);
  llvm::sys::path::remove_filename(executableDir);

  llvm::SmallString<256> colocated(executableDir);
  llvm::sys::path::append(colocated, getRuntimeLibraryFilename());
  addUniquePath(paths, colocated);

  llvm::SmallString<256> siblingLibDir(executableDir);
  llvm::sys::path::append(siblingLibDir, "..", "lib",
                          getRuntimeLibraryFilename());
  addUniquePath(paths, siblingLibDir);

  llvm::SmallString<256> siblingLib64Dir(executableDir);
  llvm::sys::path::append(siblingLib64Dir, "..", "lib64",
                          getRuntimeLibraryFilename());
  addUniquePath(paths, siblingLib64Dir);
}

std::optional<std::string>
findRuntimeLibrary(const char *argv0, std::vector<std::string> &searchedPaths) {
  if (!runtimeLibrary.empty()) {
    addUniquePath(searchedPaths, runtimeLibrary);
    if (llvm::sys::fs::is_regular_file(runtimeLibrary))
      return runtimeLibrary;
    return std::nullopt;
  }

  if (std::optional<std::string> envRuntimeLibrary =
          llvm::sys::Process::GetEnv(getRuntimeLibraryEnvVar())) {
    addUniquePath(searchedPaths, *envRuntimeLibrary);
    if (llvm::sys::fs::is_regular_file(*envRuntimeLibrary))
      return *envRuntimeLibrary;
  }

  addExecutableRelativeRuntimeCandidates(argv0, searchedPaths);
  addUniquePath(searchedPaths, getRuntimeLibraryFilename());

  for (const std::string &path : searchedPaths) {
    if (llvm::sys::fs::is_regular_file(path))
      return path;
  }
  return std::nullopt;
}

bool addRuntimeLibrary(const char *argv0, std::vector<std::string> &libraries) {
  std::vector<std::string> searchedPaths;
  std::optional<std::string> path = findRuntimeLibrary(argv0, searchedPaths);
  if (!path) {
    llvm::errs() << "error: RustToLLVM runtime shim not found\n";
    llvm::errs() << "searched:\n";
    for (const std::string &searchedPath : searchedPaths)
      llvm::errs() << "  " << searchedPath << "\n";
    llvm::errs() << "pass --runtime-library=PATH or set "
                 << getRuntimeLibraryEnvVar() << " to override\n";
    return false;
  }

  if (std::find(libraries.begin(), libraries.end(), *path) != libraries.end())
    return true;

  auto insertIt = libraries.begin();
  if (insertIt != libraries.end() &&
      isRustStdSharedLibrary(llvm::sys::path::filename(*insertIt)))
    ++insertIt;
  libraries.insert(insertIt, *path);
  return true;
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

  std::vector<std::string> resolvedSharedLibs(sharedLibs.begin(),
                                              sharedLibs.end());
  if (!addDiscoveredRustStdSharedLibrary(resolvedSharedLibs))
    return 1;
  if (!addRuntimeLibrary(argv[0], resolvedSharedLibs))
    return 1;

  llvm::SmallVector<llvm::StringRef> sharedLibRefs;
  sharedLibRefs.reserve(resolvedSharedLibs.size());
  for (const std::string &lib : resolvedSharedLibs)
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
  return invokeEntryPoint(*engine);
}
