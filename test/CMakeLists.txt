set(RUST_TO_LLVM_CARGO_SUBSTITUTION "${RUST_TO_LLVM_CARGO_EXECUTABLE}")
set(RUST_TO_LLVM_RUSTC_SUBSTITUTION "${RUST_TO_LLVM_RUSTC_EXECUTABLE}")

configure_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py
  MAIN_CONFIG
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py
)

set(RUST_TO_LLVM_TEST_DEPENDS
  FileCheck
  not
  rust-cpu-runner
  rust-opt
  rust-mir-extract
)
if(NOT TARGET rust-mir-extract)
  message(FATAL_ERROR
    "RustToLLVM tests require the rust-mir-extract target.")
endif()

add_lit_testsuite(check-rust-to-llvm "Running RustToLLVM regression tests"
  ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS ${RUST_TO_LLVM_TEST_DEPENDS}
)
set_target_properties(check-rust-to-llvm PROPERTIES FOLDER "Tests")

add_custom_target(check-rust
  DEPENDS check-rust-to-llvm
)
set_target_properties(check-rust PROPERTIES FOLDER "Tests")

add_lit_testsuites(RUST_TO_LLVM ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS ${RUST_TO_LLVM_TEST_DEPENDS}
)
