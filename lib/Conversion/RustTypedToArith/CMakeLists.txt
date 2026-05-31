add_mlir_conversion_library(RustToLLVMRustTypedToArith
  RustTypedToArith.cpp

  DEPENDS
  RustToLLVMRustTypedToArithPassIncGen

  LINK_LIBS PUBLIC
  RustToLLVMRustMIRDialect
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
