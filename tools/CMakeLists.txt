add_subdirectory(rust-opt)
add_subdirectory(rust-cpu-runner)

set(RUST_MIR_EXTRACT_BINARY
  ${CMAKE_CURRENT_BINARY_DIR}/rust-mir-extract-target/debug/rust-mir-extract${CMAKE_EXECUTABLE_SUFFIX})
set(RUST_MIR_EXTRACT_OUTPUT
  ${LLVM_RUNTIME_OUTPUT_INTDIR}/rust-mir-extract${CMAKE_EXECUTABLE_SUFFIX})

set(RUST_MIR_EXTRACT_CARGO_ENV
  CARGO_TARGET_DIR=${CMAKE_CURRENT_BINARY_DIR}/rust-mir-extract-target
  RUSTC=${RUST_TO_LLVM_RUSTC_EXECUTABLE}
  RUST_TO_LLVM_CARGO_EXECUTABLE=${RUST_TO_LLVM_CARGO_EXECUTABLE}
  RUST_TO_LLVM_RUSTC_EXECUTABLE=${RUST_TO_LLVM_RUSTC_EXECUTABLE}
  RUST_TO_LLVM_CAPI_LIBRARY=$<TARGET_FILE:RustToLLVMRustCAPI>)

add_custom_command(
  OUTPUT ${RUST_MIR_EXTRACT_OUTPUT}
  COMMAND ${CMAKE_COMMAND} -E env
          ${RUST_MIR_EXTRACT_CARGO_ENV}
          ${RUST_TO_LLVM_CARGO_EXECUTABLE} build
          --manifest-path ${CMAKE_CURRENT_SOURCE_DIR}/rust-mir-extract/Cargo.toml
          --locked
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${RUST_MIR_EXTRACT_BINARY}
          ${RUST_MIR_EXTRACT_OUTPUT}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/rust-mir-extract
  DEPENDS
    ${PROJECT_SOURCE_DIR}/rust-toolchain.toml
    ${CMAKE_CURRENT_SOURCE_DIR}/rust-mir-extract/Cargo.toml
    ${CMAKE_CURRENT_SOURCE_DIR}/rust-mir-extract/Cargo.lock
    ${CMAKE_CURRENT_SOURCE_DIR}/rust-mir-extract/.cargo/config.toml
    ${CMAKE_CURRENT_SOURCE_DIR}/rust-mir-extract/build.rs
    ${CMAKE_CURRENT_SOURCE_DIR}/rust-mir-extract/src/main.rs
    RustToLLVMRustCAPI
  COMMENT "Building rust-mir-extract with Cargo"
  VERBATIM)
add_custom_target(rust-mir-extract
  DEPENDS ${RUST_MIR_EXTRACT_OUTPUT})
