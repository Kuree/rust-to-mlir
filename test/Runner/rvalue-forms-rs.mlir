// RUN: %rust_mir_extract --crate-root %S/../Inputs/rvalue_forms.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-cpu-runner %t.mlirbc -e 'rvalue_forms::run_rvalue_forms_typed' | FileCheck %s

// CHECK: 14
// CHECK-NEXT: 0
