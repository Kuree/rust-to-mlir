add_library(RustToLLVMRustCAPI SHARED
  Rust.cpp
)
llvm_update_compile_flags(RustToLLVMRustCAPI)
target_compile_definitions(RustToLLVMRustCAPI
  PRIVATE
  MLIR_CAPI_BUILDING_LIBRARY=1
)
target_link_libraries(RustToLLVMRustCAPI
  PRIVATE
  RustToLLVMRustMIRDialect
  MLIRBytecodeWriter
  MLIRDLTIDialect
  MLIRIR
  MLIRParser
  MLIRSupport
  LLVMSupport
)
set_target_properties(RustToLLVMRustCAPI PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${LLVM_LIBRARY_OUTPUT_INTDIR}
  RUNTIME_OUTPUT_DIRECTORY ${LLVM_RUNTIME_OUTPUT_INTDIR}
)
