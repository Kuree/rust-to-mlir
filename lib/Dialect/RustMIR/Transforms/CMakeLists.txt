add_mlir_library(RustToMLIRRustMIRTransforms
  LiftTypedMIR.cpp

  ADDITIONAL_HEADER_DIRS
  ${PROJECT_SOURCE_DIR}/include/mlir/Dialect/RustMIR/Transforms

  DEPENDS
  RustToMLIRRustMIRTransformsPassIncGen

  LINK_LIBS PUBLIC
  RustToMLIRRustMIRDialect
  MLIRIR
  MLIRPass
  MLIRSupport
)
