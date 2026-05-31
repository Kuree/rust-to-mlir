cmake_minimum_required(VERSION 3.20.0)

project(RustToLLVM LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard to conform to")
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)
set(CMAKE_BUILD_WITH_INSTALL_NAME_DIR ON)

find_package(MLIR REQUIRED CONFIG)

if(NOT LLVM_PACKAGE_VERSION VERSION_GREATER_EQUAL "20" OR
   NOT LLVM_PACKAGE_VERSION VERSION_LESS "21")
  message(FATAL_ERROR
    "RustToLLVM requires LLVM/MLIR 20.x; found LLVM ${LLVM_PACKAGE_VERSION}")
endif()

message(STATUS "Using MLIRConfig.cmake in: ${MLIR_DIR}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "Using LLVM/MLIR version: ${LLVM_PACKAGE_VERSION}")

set(LLVM_RUNTIME_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/bin)
set(LLVM_LIBRARY_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/lib)
set(MLIR_BINARY_DIR ${CMAKE_BINARY_DIR})

list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}")
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")

include(TableGen)
include(AddLLVM)
include(AddMLIR)
include(HandleLLVMOptions)

# HandleLLVMOptions enables -Woverloaded-virtual, which GCC reports for MLIR's
# own OpConversionPattern templates (the inherited RewritePattern::rewrite is
# "hidden" by the adaptor-based rewrite/matchAndRewrite). The noise comes from
# MLIR headers instantiated by our conversion passes, not from our code, so
# silence it where the compiler supports the flag.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-Wno-overloaded-virtual"
  RUST_TO_LLVM_HAS_WNO_OVERLOADED_VIRTUAL)
if(RUST_TO_LLVM_HAS_WNO_OVERLOADED_VIRTUAL)
  add_compile_options(-Wno-overloaded-virtual)
endif()

set(RUST_TO_LLVM_SOURCE_DIR ${PROJECT_SOURCE_DIR})
set(RUST_TO_LLVM_BINARY_DIR ${PROJECT_BINARY_DIR})

find_program(RUST_TO_LLVM_CARGO_EXECUTABLE
  NAMES cargo
  HINTS "$ENV{HOME}/.cargo/bin"
  DOC "Path to the Cargo executable used by rust-mir-extract and lit tests")
find_program(RUST_TO_LLVM_RUSTC_EXECUTABLE
  NAMES rustc
  HINTS "$ENV{HOME}/.cargo/bin"
  DOC "Path to the rustc executable used by rust-mir-extract and lit tests")

if(NOT RUST_TO_LLVM_CARGO_EXECUTABLE OR
   NOT EXISTS "${RUST_TO_LLVM_CARGO_EXECUTABLE}")
  message(FATAL_ERROR
    "RustToLLVM requires Cargo to build rust-mir-extract. "
    "Install Cargo or configure RUST_TO_LLVM_CARGO_EXECUTABLE.")
endif()

if(NOT RUST_TO_LLVM_RUSTC_EXECUTABLE OR
   NOT EXISTS "${RUST_TO_LLVM_RUSTC_EXECUTABLE}")
  message(FATAL_ERROR
    "RustToLLVM requires rustc to build and run rust-mir-extract. "
    "Install rustc or configure RUST_TO_LLVM_RUSTC_EXECUTABLE.")
endif()

message(STATUS "Using Cargo executable: ${RUST_TO_LLVM_CARGO_EXECUTABLE}")
message(STATUS "Using rustc executable: ${RUST_TO_LLVM_RUSTC_EXECUTABLE}")

execute_process(
  COMMAND ${RUST_TO_LLVM_RUSTC_EXECUTABLE} --print target-libdir
  OUTPUT_VARIABLE RUST_TO_LLVM_RUST_TARGET_LIBDIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE RUST_TO_LLVM_RUST_TARGET_LIBDIR_RESULT
)
if(NOT RUST_TO_LLVM_RUST_TARGET_LIBDIR_RESULT EQUAL 0 OR
   NOT RUST_TO_LLVM_RUST_TARGET_LIBDIR)
  message(FATAL_ERROR
    "Failed to query Rust target libdir from ${RUST_TO_LLVM_RUSTC_EXECUTABLE}")
endif()
message(STATUS "Using Rust target libdir: ${RUST_TO_LLVM_RUST_TARGET_LIBDIR}")

include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${MLIR_INCLUDE_DIRS})
include_directories(${RUST_TO_LLVM_SOURCE_DIR}/include)
include_directories(${RUST_TO_LLVM_BINARY_DIR}/include)
link_directories(${LLVM_BUILD_LIBRARY_DIR})
add_definitions(${LLVM_DEFINITIONS})

add_subdirectory(include)
add_subdirectory(lib)
add_subdirectory(runtime)
add_subdirectory(tools)

option(RUST_TO_LLVM_INCLUDE_TESTS "Generate RustToLLVM lit test targets" ON)
if(RUST_TO_LLVM_INCLUDE_TESTS)
  add_subdirectory(test)
endif()
