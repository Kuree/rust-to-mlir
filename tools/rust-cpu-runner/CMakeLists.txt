set(LLVM_LINK_COMPONENTS
  Support
)

add_mlir_tool(rust-cpu-runner
  rust-cpu-runner.cpp
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
  LLVMSupport
)

mlir_check_link_libraries(rust-cpu-runner)
