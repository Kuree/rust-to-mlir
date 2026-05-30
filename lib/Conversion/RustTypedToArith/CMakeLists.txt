add_mlir_conversion_library(RustToLLVMRustTypedToArith
  RustTypedToArith.cpp

  DEPENDS
  RustToLLVMRustTypedToArithPassIncGen

  LINK_LIBS PUBLIC
  RustToLLVMRustMIRDialect
  MLIRArithDialect
  MLIRDataLayoutInterfaces
  MLIRIR
  MLIRPass
  MLIRSupport
  MLIRTransformUtils
  MLIRUBDialect
)
