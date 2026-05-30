// RUN: %rust_mir_extract --crate-root %s -S | FileCheck %s

pub fn mask(a: usize, b: usize) -> usize {
    a & b
}

// CHECK: rust.mir.assign
// CHECK: rust.mir.place 0
// CHECK: rust.mir.binary_op
// CHECK-SAME: op = #rust.binary_op<BitAnd>
// CHECK: rust.mir.copy
// CHECK: rust.mir.place 1
// CHECK: rust.mir.copy
// CHECK: rust.mir.place 2
