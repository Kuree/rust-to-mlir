// RUN: %rust_mir_extract --crate-root %S/../Inputs/cast_int.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-cpu-runner %t.mlirbc -e 'cast_int::run_cast_int_typed' | FileCheck %s

// CHECK: -7
// CHECK-NEXT: 7
// CHECK-NEXT: 2
// CHECK-NEXT: -2
