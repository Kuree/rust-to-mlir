// RUN: %rust_mir_extract --crate-root %S/../Inputs/loop_forms.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-cpu-runner %t.mlirbc -e 'loop_forms::run_loop_forms_typed' | FileCheck %s

// CHECK: 8
// CHECK-NEXT: 9
// CHECK-NEXT: 44
// CHECK-NEXT: 10
