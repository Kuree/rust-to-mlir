// RUN: %rust_mir_extract --crate-root %S/../Inputs/control_flow_runner.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-cpu-runner %t.mlirbc -e 'control_flow_runner::run_control_flow_typed' | FileCheck %s

// CHECK: 73
// CHECK-NEXT: 147
// CHECK-NEXT: 15
// CHECK-NEXT: 99
// CHECK-NEXT: 72
