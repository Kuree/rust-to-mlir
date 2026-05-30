// RUN: %rust_mir_extract --crate-root %S/../Inputs/arith.rs -S | FileCheck %s

// CHECK: module
// CHECK: rust.mir.func @"arith::mask"
// CHECK: rust.mir.assign
// CHECK: rust.mir.binary_op
// CHECK-SAME: op = #rust.binary_op<BitAnd>
