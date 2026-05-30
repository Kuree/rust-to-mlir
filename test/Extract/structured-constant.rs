// RUN: %rust_mir_extract --crate-root %s -S | FileCheck %s

pub fn add_flag(a: usize) -> usize {
    a & 3
}

// CHECK: rust.mir.assign
// CHECK-SAME: rvalue = {kind = "BinaryOp"
// CHECK-SAME: lhs = {kind = "Copy", place = {local = 1 : i64, projection = []}}
// CHECK-SAME: op = "BitAnd"
// CHECK-SAME: rhs = {debug = "3", kind = "Constant"
// CHECK-SAME: ty =
// CHECK-SAME: value = 3 : i64
