// RUN: rust-translate --rs %S/../Inputs/call.rs --rust-mir-extract %rust_mir_extract | FileCheck %s

// CHECK: module attributes
// CHECK-SAME: dlti.dl_spec = #dlti.dl_spec<index = 64 : i32, "dlti.endianness" = "little">
// CHECK-SAME: rust.mir.source = "rustc_public"
// CHECK-SAME: rust.mir.target_pointer_width = 64 : i64
// CHECK: rust.mir.func
// CHECK-SAME: rust_name = "call::inc"
// CHECK: rust.mir.checked_binary_op
// CHECK: rust.mir.binop_add
// CHECK: rust.mir.constant
// CHECK: rust.mir.assert_overflow
// CHECK: rust.mir.func
// CHECK-SAME: rust_name = "call::caller"
// CHECK: rust.mir.call
// CHECK: rust.mir.constant
// CHECK: rust.mir.copy
// CHECK: rust.mir.unwind_continue
// CHECK: rust.mir.return
