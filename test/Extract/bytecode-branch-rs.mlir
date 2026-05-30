// RUN: %rust_mir_extract --crate-root %S/../Inputs/branch.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc | FileCheck %s

// CHECK-LABEL: rust.mir.func @"branch::choose"
// CHECK: rust.mir.switch_int
// CHECK: rust.mir.goto
// CHECK: rust.mir.return
