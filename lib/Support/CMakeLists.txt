add_mlir_library(RustToLLVMSupport
  Toolchain.cpp

  ADDITIONAL_HEADER_DIRS
  ${PROJECT_SOURCE_DIR}/include/RustToLLVM/Support

  LINK_LIBS PUBLIC
  RustToLLVMRustMIRDialect
  RustToLLVMRustMIRTransforms
  RustToLLVMRustTypedToArith
  RustToLLVMRustTypedToControlFlow
  RustToLLVMRustTypedToFunc
  RustToLLVMRustTypedMemoryToLLVM
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
