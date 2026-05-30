set(LLVM_TARGET_DEFINITIONS Passes.td)
mlir_tablegen(RustTypedToArithPasses.h.inc -gen-pass-decls
  -name RustTypedToArith)
add_public_tablegen_target(RustToLLVMRustTypedToArithPassIncGen)
