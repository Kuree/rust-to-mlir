set(LLVM_TARGET_DEFINITIONS Passes.td)
mlir_tablegen(RustTypedToControlFlowPasses.h.inc -gen-pass-decls
  -name RustTypedToControlFlow)
add_public_tablegen_target(RustToLLVMRustTypedToControlFlowPassIncGen)
