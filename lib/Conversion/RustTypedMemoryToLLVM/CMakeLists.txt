add_mlir_conversion_library(RustToLLVMRustTypedMemoryToLLVM
  RustTypedMemoryToLLVM.cpp

  DEPENDS
  RustToLLVMRustTypedMemoryToLLVMPassIncGen

  LINK_LIBS PUBLIC
  RustToLLVMRustMIRDialect
  MLIRFuncDialect
  MLIRFuncTransforms
  MLIRIR
  MLIRLLVMDialect
  MLIRPass
  MLIRSupport
  MLIRTransformUtils
)
