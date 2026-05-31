cmake_minimum_required(VERSION 3.20.0)

project(RustToMLIR LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard to conform to")
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)
set(CMAKE_BUILD_WITH_INSTALL_NAME_DIR ON)

find_package(MLIR REQUIRED CONFIG)

if(NOT LLVM_PACKAGE_VERSION VERSION_GREATER_EQUAL "20" OR
   NOT LLVM_PACKAGE_VERSION VERSION_LESS "23")
  message(FATAL_ERROR
    "RustToMLIR requires LLVM/MLIR 20.x through 22.x; "
    "found LLVM ${LLVM_PACKAGE_VERSION}")
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

if(TARGET MLIR)
  set(MLIR_LINK_MLIR_DYLIB ON CACHE BOOL
    "Link against libMLIR instead of MLIR component archives" FORCE)
endif()

if(LLVM_LINK_LLVM_DYLIB)
  set(RUST_TO_MLIR_NATIVE_TARGET_LIBS)
else()
  llvm_map_components_to_libnames(RUST_TO_MLIR_NATIVE_TARGET_LIBS
    native
    asmparser
    asmprinter
  )
endif()

if(TARGET LLVMSupport AND TARGET zstd::libzstd_static AND
   TARGET zstd::libzstd_shared)
  # Official LLVM binary archives may export static zstd through LLVMSupport;
  # use shared zstd so RustToMLIR's shared C API can link on distro systems
  # where libzstd.a is not built as PIC.
  get_target_property(RUST_TO_MLIR_LLVM_SUPPORT_LINK_LIBS LLVMSupport
    INTERFACE_LINK_LIBRARIES)
  if(RUST_TO_MLIR_LLVM_SUPPORT_LINK_LIBS)
    set(RUST_TO_MLIR_REWRITTEN_LLVM_SUPPORT_LINK_LIBS)
    foreach(LIB ${RUST_TO_MLIR_LLVM_SUPPORT_LINK_LIBS})
      if(LIB STREQUAL "zstd::libzstd_static")
        list(APPEND RUST_TO_MLIR_REWRITTEN_LLVM_SUPPORT_LINK_LIBS
          zstd::libzstd_shared)
      else()
        list(APPEND RUST_TO_MLIR_REWRITTEN_LLVM_SUPPORT_LINK_LIBS ${LIB})
      endif()
    endforeach()
    set_target_properties(LLVMSupport PROPERTIES
      INTERFACE_LINK_LIBRARIES
      "${RUST_TO_MLIR_REWRITTEN_LLVM_SUPPORT_LINK_LIBS}")
  endif()
endif()

# HandleLLVMOptions enables -Woverloaded-virtual, which GCC reports for MLIR's
# own OpConversionPattern templates (the inherited RewritePattern::rewrite is
# "hidden" by the adaptor-based rewrite/matchAndRewrite). The noise comes from
# MLIR headers instantiated by our conversion passes, not from our code, so
# silence it where the compiler supports the flag.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-Wno-overloaded-virtual"
  RUST_TO_MLIR_HAS_WNO_OVERLOADED_VIRTUAL)
if(RUST_TO_MLIR_HAS_WNO_OVERLOADED_VIRTUAL)
  add_compile_options(-Wno-overloaded-virtual)
endif()

set(RUST_TO_MLIR_SOURCE_DIR ${PROJECT_SOURCE_DIR})
set(RUST_TO_MLIR_BINARY_DIR ${PROJECT_BINARY_DIR})

find_program(RUST_TO_MLIR_CARGO_EXECUTABLE
  NAMES cargo
  HINTS "$ENV{HOME}/.cargo/bin"
  DOC "Path to the Cargo executable used by rust-mir-extract and lit tests")
find_program(RUST_TO_MLIR_RUSTC_EXECUTABLE
  NAMES rustc
  HINTS "$ENV{HOME}/.cargo/bin"
  DOC "Path to the rustc executable used by rust-mir-extract and lit tests")

if(NOT RUST_TO_MLIR_CARGO_EXECUTABLE OR
   NOT EXISTS "${RUST_TO_MLIR_CARGO_EXECUTABLE}")
  message(FATAL_ERROR
    "RustToMLIR requires Cargo to build rust-mir-extract. "
    "Install Cargo or configure RUST_TO_MLIR_CARGO_EXECUTABLE.")
endif()

if(NOT RUST_TO_MLIR_RUSTC_EXECUTABLE OR
   NOT EXISTS "${RUST_TO_MLIR_RUSTC_EXECUTABLE}")
  message(FATAL_ERROR
    "RustToMLIR requires rustc to build and run rust-mir-extract. "
    "Install rustc or configure RUST_TO_MLIR_RUSTC_EXECUTABLE.")
endif()

message(STATUS "Using Cargo executable: ${RUST_TO_MLIR_CARGO_EXECUTABLE}")
message(STATUS "Using rustc executable: ${RUST_TO_MLIR_RUSTC_EXECUTABLE}")

set(RUST_TO_MLIR_DEFAULT_PYTHON_EXECUTABLE
  "${PROJECT_SOURCE_DIR}/env/bin/python")
if(EXISTS "${RUST_TO_MLIR_DEFAULT_PYTHON_EXECUTABLE}")
  set(RUST_TO_MLIR_PYTHON_EXECUTABLE
    "${RUST_TO_MLIR_DEFAULT_PYTHON_EXECUTABLE}"
    CACHE FILEPATH "Python executable used to build RustToMLIR wheels")
else()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(RUST_TO_MLIR_PYTHON_EXECUTABLE "${Python3_EXECUTABLE}"
    CACHE FILEPATH "Python executable used to build RustToMLIR wheels")
endif()
message(STATUS "Using Python executable for wheel builds: ${RUST_TO_MLIR_PYTHON_EXECUTABLE}")

execute_process(
  COMMAND ${RUST_TO_MLIR_RUSTC_EXECUTABLE} --print target-libdir
  OUTPUT_VARIABLE RUST_TO_MLIR_RUST_TARGET_LIBDIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE RUST_TO_MLIR_RUST_TARGET_LIBDIR_RESULT
)
if(NOT RUST_TO_MLIR_RUST_TARGET_LIBDIR_RESULT EQUAL 0 OR
   NOT RUST_TO_MLIR_RUST_TARGET_LIBDIR)
  message(FATAL_ERROR
    "Failed to query Rust target libdir from ${RUST_TO_MLIR_RUSTC_EXECUTABLE}")
endif()
message(STATUS "Using Rust target libdir: ${RUST_TO_MLIR_RUST_TARGET_LIBDIR}")

include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${MLIR_INCLUDE_DIRS})
include_directories(${RUST_TO_MLIR_SOURCE_DIR}/include)
include_directories(${RUST_TO_MLIR_BINARY_DIR}/include)
link_directories(${LLVM_BUILD_LIBRARY_DIR})
add_definitions(${LLVM_DEFINITIONS})

add_subdirectory(include)
add_subdirectory(lib)
add_subdirectory(runtime)
add_subdirectory(tools)

option(RUST_TO_MLIR_INCLUDE_TESTS "Generate RustToMLIR lit test targets" ON)
if(RUST_TO_MLIR_INCLUDE_TESTS)
  add_subdirectory(test)
endif()

set(RUST_TO_MLIR_WHEEL_DIST_DIR "${PROJECT_SOURCE_DIR}/dist"
  CACHE PATH "Directory for auditwheel-repaired RustToMLIR wheels")
set(RUST_TO_MLIR_AUDITWHEEL_PLAT ""
  CACHE STRING "Optional auditwheel --plat value for RustToMLIR wheels")

set(RUST_TO_MLIR_WHEEL_ARGS
  --build-dir "${PROJECT_BINARY_DIR}"
  --raw-dir "${PROJECT_BINARY_DIR}/python-wheel"
  --dist-dir "${RUST_TO_MLIR_WHEEL_DIST_DIR}"
)
if(RUST_TO_MLIR_AUDITWHEEL_PLAT)
  list(APPEND RUST_TO_MLIR_WHEEL_ARGS
    --plat "${RUST_TO_MLIR_AUDITWHEEL_PLAT}")
endif()
if(CMAKE_CONFIGURATION_TYPES)
  list(APPEND RUST_TO_MLIR_WHEEL_ARGS --config "$<CONFIG>")
endif()

add_custom_target(wheel
  COMMAND ${CMAKE_COMMAND} -E env
          RUST_TO_MLIR_CMAKE_BINARY_DIR=${PROJECT_BINARY_DIR}
          ${RUST_TO_MLIR_PYTHON_EXECUTABLE}
          ${PROJECT_SOURCE_DIR}/python/build_wheel.py
          ${RUST_TO_MLIR_WHEEL_ARGS}
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/python
  DEPENDS
    RustToMLIRRustCAPI
    rust-cpu-runner
    rust-mir-extract
    rust-opt
    rust-run
    rust-to-mlir-runtime
  COMMENT "Building auditwheel-repaired RustToMLIR wheel in ${RUST_TO_MLIR_WHEEL_DIST_DIR}"
  VERBATIM)
