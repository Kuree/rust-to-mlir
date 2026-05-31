// RUN: %rust_mir_extract --crate-root %S/../Inputs/cast_float.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-cpu-runner %t.mlirbc -e 'cast_float::run_cast_float_typed' | FileCheck %s

// CHECK: 1.5
// CHECK-NEXT: 2.75
// CHECK-NEXT: -7
// CHECK-NEXT: 42
// CHECK-NEXT: -3
// CHECK-NEXT: 4
// CHECK-NEXT: 2147483647
// CHECK-NEXT: 0
// CHECK-NEXT: 0
// CHECK-NEXT: 4294967295
