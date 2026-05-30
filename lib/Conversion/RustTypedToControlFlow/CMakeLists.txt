add_mlir_conversion_library(RustToLLVMRustTypedToControlFlow
  RustTypedToControlFlow.cpp

  DEPENDS
  RustToLLVMRustTypedToControlFlowPassIncGen

  LINK_LIBS PUBLIC
  RustToLLVMRustMIRDialect
  MLIRControlFlowDialect
  MLIRFuncDialect
  MLIRIR
  MLIRLLVMDialect
  MLIRPass
  MLIRSupport
  MLIRTransformUtils
)
