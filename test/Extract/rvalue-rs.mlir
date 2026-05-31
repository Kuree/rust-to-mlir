// RUN: %rust_mir_extract --crate-root %S/../Inputs/rvalue_forms.rs -S | FileCheck %s

// CHECK-LABEL: rust.mir.func @"rvalue_forms::repeat_array"
// CHECK: rust.mir.repeat
// CHECK-SAME: count = 3 : i64
// CHECK: rust.mir.copy
// CHECK: rust.mir.place 1
