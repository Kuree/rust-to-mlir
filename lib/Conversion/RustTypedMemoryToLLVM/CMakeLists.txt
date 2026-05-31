add_mlir_conversion_library(RustToMLIRRustTypedMemoryToLLVM
  RustTypedMemoryToLLVM.cpp

  DEPENDS
  RustToMLIRRustTypedMemoryToLLVMPassIncGen

  LINK_LIBS PUBLIC
  RustToMLIRRustMIRDialect
  MLIRFuncDialect
  MLIRFuncTransforms
  MLIRIR
  MLIRLLVMDialect
  MLIRPass
  MLIRSupport
  MLIRTransformUtils
)
