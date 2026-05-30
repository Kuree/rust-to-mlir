set(LLVM_TARGET_DEFINITIONS Passes.td)
mlir_tablegen(RustTypedMemoryToLLVMPasses.h.inc -gen-pass-decls
  -name RustTypedMemoryToLLVM)
add_public_tablegen_target(RustToLLVMRustTypedMemoryToLLVMPassIncGen)
