add_mlir_library(RustToMLIRSupport
  Toolchain.cpp

  ADDITIONAL_HEADER_DIRS
  ${PROJECT_SOURCE_DIR}/include/RustToMLIR/Support

  LINK_LIBS PUBLIC
  RustToMLIRRustMIRDialect
  RustToMLIRRustMIRTransforms
  RustToMLIRRustTypedToArith
  RustToMLIRRustTypedToControlFlow
  RustToMLIRRustTypedToFunc
  RustToMLIRRustTypedMemoryToLLVM
  MLIRArithDialect
  MLIRArithToLLVM
  MLIRBuiltinToLLVMIRTranslation
  MLIRControlFlowDialect
  MLIRControlFlowToLLVM
  MLIRDLTIDialect
  MLIRFuncDialect
  MLIRFuncToLLVM
  MLIRIndexDialect
  MLIRIndexToLLVM
  MLIRIR
  MLIRLLVMToLLVMIRTranslation
  MLIRLLVMDialect
  MLIRMathDialect
  MLIRMathToLLVM
  MLIRPass
  MLIRReconcileUnrealizedCasts
  MLIRSupport
  MLIRTransforms
  MLIRUBDialect
  MLIRUBToLLVM
)
