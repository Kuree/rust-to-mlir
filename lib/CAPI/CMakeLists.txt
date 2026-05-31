add_library(RustToMLIRRustCAPI SHARED
  Rust.cpp
)
llvm_update_compile_flags(RustToMLIRRustCAPI)
target_compile_definitions(RustToMLIRRustCAPI
  PRIVATE
  MLIR_CAPI_BUILDING_LIBRARY=1
)
target_link_libraries(RustToMLIRRustCAPI
  PRIVATE
  RustToMLIRSupport
  RustToMLIRRustMIRDialect
  ${RUST_TO_MLIR_NATIVE_TARGET_LIBS}
)
mlir_target_link_libraries(RustToMLIRRustCAPI
  PRIVATE
  MLIRBytecodeWriter
  MLIRDLTIDialect
  MLIRExecutionEngine
  MLIRIR
  MLIRParser
  MLIRPass
  MLIRSupport
)
set_target_properties(RustToMLIRRustCAPI PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${LLVM_LIBRARY_OUTPUT_INTDIR}
  RUNTIME_OUTPUT_DIRECTORY ${LLVM_RUNTIME_OUTPUT_INTDIR}
)
