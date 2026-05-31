add_mlir_tool(rust-cpu-runner
  rust-cpu-runner.cpp
)
add_dependencies(rust-cpu-runner
  rust-to-llvm-runtime
)
llvm_update_compile_flags(rust-cpu-runner)
target_link_libraries(rust-cpu-runner
  PRIVATE
  RustToLLVMSupport
  MLIRExecutionEngine
  MLIRIR
  MLIRParser
  MLIRPass
  MLIRSupport
)

mlir_check_link_libraries(rust-cpu-runner)
