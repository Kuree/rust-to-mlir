// RUN: %rust_mir_extract --cargo %S/../Inputs/cargo-basic -S -o %t.mlir
// RUN: FileCheck %s < %t.mlir

// CHECK-LABEL: rust.mir.func @"cargo_basic::twice"
// CHECK: rust.mir.local
// CHECK: rust.mir.assign
// CHECK: rust.mir.return
