set(RUST_TO_LLVM_CARGO_SUBSTITUTION "")
if(RUST_TO_LLVM_CARGO_EXECUTABLE)
  set(RUST_TO_LLVM_CARGO_SUBSTITUTION "${RUST_TO_LLVM_CARGO_EXECUTABLE}")
endif()

set(RUST_TO_LLVM_RUSTC_SUBSTITUTION "")
if(RUST_TO_LLVM_RUSTC_EXECUTABLE)
  set(RUST_TO_LLVM_RUSTC_SUBSTITUTION "${RUST_TO_LLVM_RUSTC_EXECUTABLE}")
endif()

configure_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py
  MAIN_CONFIG
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py
)

set(RUST_TO_LLVM_TEST_DEPENDS
  FileCheck
  not
  rust-opt
  rust-translate
)
if(TARGET rust-mir-extract)
  list(APPEND RUST_TO_LLVM_TEST_DEPENDS rust-mir-extract)
endif()

add_lit_testsuite(check-rust-to-llvm "Running RustToLLVM regression tests"
  ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS ${RUST_TO_LLVM_TEST_DEPENDS}
)
set_target_properties(check-rust-to-llvm PROPERTIES FOLDER "Tests")

add_lit_testsuites(RUST_TO_LLVM ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS ${RUST_TO_LLVM_TEST_DEPENDS}
)
