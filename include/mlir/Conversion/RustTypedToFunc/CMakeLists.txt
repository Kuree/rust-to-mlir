set(LLVM_TARGET_DEFINITIONS Passes.td)
mlir_tablegen(RustTypedToFuncPasses.h.inc -gen-pass-decls
  -name RustTypedToFunc)
add_public_tablegen_target(RustToMLIRRustTypedToFuncPassIncGen)
