add_mlir_conversion_library(RustToLLVMRustTypedToFunc
  RustTypedToFunc.cpp

  DEPENDS
  RustToLLVMRustTypedToFuncPassIncGen

  LINK_LIBS PUBLIC
  RustToLLVMRustMIRDialect
  MLIRFuncDialect
  MLIRIR
  MLIRPass
  MLIRSupport
  MLIRTransformUtils
)
