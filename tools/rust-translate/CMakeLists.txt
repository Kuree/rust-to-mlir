set(LLVM_LINK_COMPONENTS
  Support
)

add_mlir_tool(rust-translate
  rust-translate.cpp
)
llvm_update_compile_flags(rust-translate)
target_link_libraries(rust-translate
  PRIVATE
  RustToLLVMRustTarget
  RustToLLVMRustMIRDialect
  MLIRIR
  MLIRSupport
  LLVMSupport
)

mlir_check_link_libraries(rust-translate)
