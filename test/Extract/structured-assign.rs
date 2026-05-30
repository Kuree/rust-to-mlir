// RUN: %rust_mir_extract --crate-root %s -S | FileCheck %s

pub fn mask(a: usize, b: usize) -> usize {
    a & b
}

// CHECK: rust.mir.assign
// CHECK-SAME: place = {local = 0 : i64, projection = []}
// CHECK-SAME: rvalue = {kind = "BinaryOp"
// CHECK-SAME: lhs = {kind = "Copy", place = {local = 1 : i64, projection = []}}
// CHECK-SAME: op = "BitAnd"
// CHECK-SAME: rhs = {kind = "Copy", place = {local = 2 : i64, projection = []}}
