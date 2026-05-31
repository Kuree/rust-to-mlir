add_mlir_conversion_library(RustToMLIRRustTypedToControlFlow
  RustTypedToControlFlow.cpp

  DEPENDS
  RustToMLIRRustTypedToControlFlowPassIncGen

  LINK_LIBS PUBLIC
  RustToMLIRRustMIRDialect
  MLIRControlFlowDialect
  MLIRFuncDialect
  MLIRIR
  MLIRLLVMDialect
  MLIRPass
  MLIRSupport
  MLIRTransformUtils
)
