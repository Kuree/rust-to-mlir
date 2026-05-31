// RUN: %rust_mir_extract --crate-root %S/../Inputs/drop.rs -S | FileCheck %s

// CHECK: rust.mir.drop
// CHECK-SAME: unwind = #rust.unwind<Continue>
// CHECK: rust.mir.place 2
