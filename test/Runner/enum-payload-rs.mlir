// RUN: %rust_mir_extract --crate-root %S/../Inputs/enum_payload.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-cpu-runner %t.mlirbc -e 'enum_payload::run_enum_payload_typed' | FileCheck %s

// CHECK: 5
// CHECK-NEXT: 7
// CHECK-NEXT: 4
// CHECK-NEXT: 5
// CHECK-NEXT: 17
// CHECK-NEXT: 27
// CHECK-NEXT: 34
// CHECK-NEXT: 8
// CHECK-NEXT: 20
// CHECK-NEXT: 30
