// RUN: %rust_mir_extract --crate-root %S/../Inputs/struct_adt.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-cpu-runner %t.mlirbc -e 'struct_adt::run_struct_adt_typed' | FileCheck %s

// CHECK: 31
