set(LLVM_LINK_COMPONENTS
  Support
)

add_mlir_tool(rust-opt
  rust-opt.cpp
)
llvm_update_compile_flags(rust-opt)
target_link_libraries(rust-opt
  PRIVATE
  RustToMLIRSupport
)
mlir_target_link_libraries(rust-opt
  PRIVATE
  MLIRIR
  MLIRMlirOptMain
  MLIRSupport
)

mlir_check_link_libraries(rust-opt)
