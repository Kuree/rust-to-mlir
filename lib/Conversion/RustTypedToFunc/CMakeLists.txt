add_mlir_conversion_library(RustToMLIRRustTypedToFunc
  RustTypedToFunc.cpp

  DEPENDS
  RustToMLIRRustTypedToFuncPassIncGen
)
target_link_libraries(RustToMLIRRustTypedToFunc
  PUBLIC
  RustToMLIRRustMIRDialect
)
mlir_target_link_libraries(RustToMLIRRustTypedToFunc
  PUBLIC
  MLIRFuncDialect
  MLIRIR
  MLIRPass
  MLIRSupport
  MLIRTransformUtils
)
