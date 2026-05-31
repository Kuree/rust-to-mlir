// RUN: %rust_mir_extract --crate-root %S/../Inputs/float_cmp_math.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-cpu-runner %t.mlirbc -e 'float_cmp_math::run_float_cmp_math_typed' | FileCheck %s --check-prefix=CHECK
// RUN: rust-cpu-runner %t.mlirbc -e 'float_cmp_math::return_f64_typed' | FileCheck %s --check-prefix=ENTRY

// CHECK: true
// CHECK-NEXT: true
// CHECK-NEXT: true
// CHECK-NEXT: true
// CHECK-NEXT: true
// CHECK-NEXT: true
// CHECK-NEXT: false
// CHECK-NEXT: true
// CHECK-NEXT: false
// CHECK-NEXT: 2
// CHECK-NEXT: 3
// CHECK-NEXT: 4
// CHECK-NEXT: 4
// CHECK-NEXT: -3
// CHECK-NEXT: 5.5
// CHECK-NEXT: 8

// ENTRY: 6.250000e+00
