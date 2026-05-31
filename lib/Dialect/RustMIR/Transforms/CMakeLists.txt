add_mlir_library(RustToMLIRRustMIRTransforms
  LiftTypedMIR.cpp

  ADDITIONAL_HEADER_DIRS
  ${PROJECT_SOURCE_DIR}/include/mlir/Dialect/RustMIR/Transforms

  DEPENDS
  RustToMLIRRustMIRTransformsPassIncGen
)
target_link_libraries(RustToMLIRRustMIRTransforms
  PUBLIC
  RustToMLIRRustMIRDialect
)
mlir_target_link_libraries(RustToMLIRRustMIRTransforms
  PUBLIC
  MLIRIR
  MLIRPass
  MLIRSupport
)
