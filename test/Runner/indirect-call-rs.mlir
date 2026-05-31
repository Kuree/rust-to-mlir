// RUN: %rust_mir_extract --crate-root %S/../Inputs/indirect_call.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-cpu-runner %t.mlirbc -e 'indirect_call::run_indirect_call_typed' | FileCheck %s

// CHECK: 6
// CHECK-NEXT: 8
// CHECK-NEXT: 14
