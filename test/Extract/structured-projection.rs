// RUN: %rust_mir_extract --crate-root %s -S | FileCheck %s

pub fn second(pair: (usize, usize)) -> usize {
    pair.1
}

// CHECK: rust.mir.assign
// CHECK: rust.mir.place 0
// CHECK: rust.mir.use
// CHECK: rust.mir.copy
// CHECK: rust.mir.place 1
// CHECK: rust.mir.projection_field
// CHECK-SAME: index = 1 : i64
