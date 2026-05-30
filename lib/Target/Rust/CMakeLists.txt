add_mlir_translation_library(RustToLLVMRustTarget
  RustImport.cpp
  RustTranslation.cpp

  ADDITIONAL_HEADER_DIRS
  ${PROJECT_SOURCE_DIR}/include/RustToLLVM/Target/Rust

  LINK_LIBS PUBLIC
  RustToLLVMRustMIRDialect
  MLIRDLTIDialect
  MLIRIR
  MLIRSupport
  MLIRTranslateLib
  LLVMSupport
)
