add_mlir_conversion_library(RustToMLIRRustTypedToFunc
  RustTypedToFunc.cpp

  DEPENDS
  RustToMLIRRustTypedToFuncPassIncGen

  LINK_LIBS PUBLIC
  RustToMLIRRustMIRDialect
  MLIRFuncDialect
  MLIRIR
  MLIRPass
  MLIRSupport
  MLIRTransformUtils
)
