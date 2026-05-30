set(LLVM_TARGET_DEFINITIONS Passes.td)
mlir_tablegen(RustMIRTransformsPasses.h.inc -gen-pass-decls
  -name RustMIRTransforms)
add_public_tablegen_target(RustToLLVMRustMIRTransformsPassIncGen)
