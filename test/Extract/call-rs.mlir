// RUN: %rust_mir_extract --crate-root %S/../Inputs/call.rs -S | FileCheck %s

// CHECK-LABEL: rust.mir.func @"call::caller"
// CHECK: rust.mir.call
// CHECK-SAME: callee_abi = "rust"
// CHECK-SAME: callee_generic_args = "GenericArgs([])"
// CHECK-SAME: callee_name = "call::inc"
// CHECK-SAME: mir_kind = "Call"
// CHECK-SAME: target = 1 : i64
// CHECK-SAME: unwind = "Continue"
// CHECK: rust.mir.constant
// CHECK-SAME: FnDef
// CHECK-SAME: call::inc
// CHECK: rust.mir.place 0
// CHECK: rust.mir.copy
// CHECK: rust.mir.place 1
