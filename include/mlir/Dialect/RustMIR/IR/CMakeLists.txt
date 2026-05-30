add_mlir_dialect(RustOps rust)
add_custom_target(MLIRRustMIROpsIncGen DEPENDS MLIRRustOpsIncGen)
add_mlir_doc(RustMIRDialect RustMIRDialect RustMIR/ -gen-dialect-doc)
add_mlir_doc(RustOps RustMIROps RustMIR/ -gen-op-doc)
