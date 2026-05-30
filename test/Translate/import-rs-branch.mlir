// RUN: rust-translate --rs %S/../Inputs/branch.rs --rust-mir-extract %rust_mir_extract | FileCheck %s

// CHECK: module attributes
// CHECK-SAME: dlti.dl_spec = #dlti.dl_spec<index = 64 : i32, "dlti.endianness" = "little">
// CHECK-SAME: rust.mir.source = "rustc_public"
// CHECK-SAME: rust.mir.target_pointer_width = 64 : i64
// CHECK: rust.mir.func
// CHECK-SAME: rust_name = "branch::choose"
// CHECK: rust.mir.binary_op
// CHECK: rust.mir.binop_gt
// CHECK: rust.mir.constant
// CHECK: rust.mir.switch_int
// CHECK: rust.mir.goto
// CHECK: rust.mir.binop_eq
// CHECK: rust.mir.assert_overflow_neg
// CHECK: rust.mir.unary_op
// CHECK: rust.mir.unop_neg
// CHECK: rust.mir.return
