set(RUST_TO_MLIR_CARGO_SUBSTITUTION "${RUST_TO_MLIR_CARGO_EXECUTABLE}")
set(RUST_TO_MLIR_RUSTC_SUBSTITUTION "${RUST_TO_MLIR_RUSTC_EXECUTABLE}")

configure_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py
  MAIN_CONFIG
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py
)

set(RUST_TO_MLIR_TEST_DEPENDS
  FileCheck
  not
  rust-cpu-runner
  rust-run
  rust-opt
  rust-mir-extract
)
if(NOT TARGET rust-mir-extract)
  message(FATAL_ERROR
    "RustToMLIR tests require the rust-mir-extract target.")
endif()

add_lit_testsuite(check-rust-to-mlir "Running RustToMLIR regression tests"
  ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS ${RUST_TO_MLIR_TEST_DEPENDS}
)
set_target_properties(check-rust-to-mlir PROPERTIES FOLDER "Tests")

add_custom_target(check-rust
  DEPENDS check-rust-to-mlir
)
set_target_properties(check-rust PROPERTIES FOLDER "Tests")

add_lit_testsuites(RUST_TO_MLIR ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS ${RUST_TO_MLIR_TEST_DEPENDS}
)
