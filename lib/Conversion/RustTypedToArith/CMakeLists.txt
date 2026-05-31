add_mlir_conversion_library(RustToMLIRRustTypedToArith
  RustTypedToArith.cpp

  DEPENDS
  RustToMLIRRustTypedToArithPassIncGen

  LINK_LIBS PUBLIC
  RustToMLIRRustMIRDialect
  MLIRArithDialect
  MLIRControlFlowDialect
  MLIRDataLayoutInterfaces
  MLIRIR
  MLIRLLVMDialect
  MLIRMathDialect
  MLIRPass
  MLIRSupport
  MLIRTransformUtils
  MLIRUBDialect
)
