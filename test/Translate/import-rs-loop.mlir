// RUN: rust-translate --rs %S/../Inputs/loop.rs --rust-mir-extract %rust_mir_extract | FileCheck %s

// CHECK: module attributes
// CHECK-SAME: dlti.dl_spec = #dlti.dl_spec<index = 64 : i32, "dlti.endianness" = "little">
// CHECK-SAME: rust.mir.source = "rustc_public"
// CHECK-SAME: rust.mir.target_pointer_width = 64 : i64
// CHECK: rust.mir.func
// CHECK-SAME: rust_name = "loop::sum_to"
// CHECK: rust.mir.constant
// CHECK: rust.mir.goto
// CHECK: rust.mir.binop_lt
// CHECK: rust.mir.switch_int
// CHECK: rust.mir.checked_binary_op
// CHECK: rust.mir.binop_add
// CHECK: rust.mir.assert_overflow
// CHECK: rust.mir.unwind_continue
// CHECK: rust.mir.return
