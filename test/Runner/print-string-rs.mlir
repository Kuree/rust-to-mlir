// RUN: %rust_mir_extract --crate-root %S/../Inputs/print_str_shim.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-cpu-runner %t.mlirbc -e 'print_str_shim::print_str_typed' | FileCheck %s

// CHECK: hello shim
