// RUN: %rust_mir_extract --crate-root %S/../Inputs/float_arith.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-cpu-runner %t.mlirbc -e 'float_arith::run_float_arith_typed' | FileCheck %s

// CHECK: 3.75
// CHECK-NEXT: -4.5
// CHECK-NEXT: 1.5
// CHECK-NEXT: 7
// CHECK-NEXT: 3
