// RUN: %rust_mir_extract --crate-root %S/../Inputs/print_variants.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-cpu-runner %t.mlirbc -e 'print_variants::print_variants_typed' | FileCheck %s

// CHECK: 12 -34
// CHECK-NEXT: true false
// CHECK-NEXT: prefix suffix
// CHECK-NEXT: A
