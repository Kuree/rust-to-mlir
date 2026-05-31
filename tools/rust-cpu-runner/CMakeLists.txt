add_mlir_tool(rust-cpu-runner
  rust-cpu-runner.cpp
)
add_dependencies(rust-cpu-runner
  rust-to-mlir-runtime
)
llvm_update_compile_flags(rust-cpu-runner)
target_link_libraries(rust-cpu-runner
  PRIVATE
  RustToMLIRSupport
  MLIRExecutionEngine
  MLIRIR
  MLIRParser
  MLIRPass
  MLIRSupport
)

mlir_check_link_libraries(rust-cpu-runner)
