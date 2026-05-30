add_mlir_library(RustToLLVMRustMIRTransforms
  LiftTypedMIR.cpp

  ADDITIONAL_HEADER_DIRS
  ${PROJECT_SOURCE_DIR}/include/mlir/Dialect/RustMIR/Transforms

  LINK_LIBS PUBLIC
  RustToLLVMRustMIRDialect
  MLIRIR
  MLIRPass
  MLIRSupport
)
