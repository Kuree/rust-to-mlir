// RUN: %rust_mir_extract --crate-root %s -S | FileCheck %s

pub fn add_flag(a: usize) -> usize {
    a & 3
}

// CHECK: rust.mir.assign
// CHECK: rust.mir.binary_op
// CHECK-SAME: op = "BitAnd"
// CHECK: rust.mir.copy
// CHECK: rust.mir.place 1
// CHECK: rust.mir.constant
// CHECK-SAME: debug = "3"
// CHECK-SAME: ty =
// CHECK-SAME: value = 3 : i64
