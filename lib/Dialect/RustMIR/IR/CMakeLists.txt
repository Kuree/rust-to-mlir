add_mlir_dialect_library(RustToLLVMRustMIRDialect
  RustMIRDialect.cpp
  RustTypes.cpp
  RustOps.cpp
  RustTypedOps.cpp

  ADDITIONAL_HEADER_DIRS
  ${PROJECT_SOURCE_DIR}/include/mlir/Dialect/RustMIR/IR
  ${PROJECT_SOURCE_DIR}/include/mlir/Dialect/RustTyped/IR

  DEPENDS
  MLIRRustMIROpsIncGen
  MLIRRustTypedOpsIncGen

  LINK_LIBS PUBLIC
  MLIRIR
  MLIRLLVMDialect
  MLIRMemorySlotInterfaces
  MLIRSupport
  MLIRSideEffectInterfaces
)

add_library(RustToLLVMRustTypedDialect INTERFACE)
target_link_libraries(RustToLLVMRustTypedDialect
  INTERFACE
  RustToLLVMRustMIRDialect
)
