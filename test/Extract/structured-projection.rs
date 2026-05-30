// RUN: %rust_mir_extract --crate-root %s -S | FileCheck %s

pub fn second(pair: (usize, usize)) -> usize {
    pair.1
}

// CHECK: rust.mir.assign
// CHECK-SAME: place = {local = 0 : i64, projection = []}
// CHECK-SAME: operand = {kind = "Copy"
// CHECK-SAME: local = 1 : i64, projection = [{index = 1 : i64, kind = "Field"
