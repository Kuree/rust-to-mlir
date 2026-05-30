// RUN: rust-translate --rs %S/../Inputs/aggregate.rs --rust-mir-extract %rust_mir_extract | FileCheck %s

// CHECK: module attributes
// CHECK-SAME: dlti.dl_spec = #dlti.dl_spec<index = 64 : i32, "dlti.endianness" = "little">
// CHECK-SAME: rust.mir.source = "rustc_public"
// CHECK-SAME: rust.mir.target_pointer_width = 64 : i64
// CHECK: rust.mir.func
// CHECK-SAME: rust_name = "aggregate::make_tuple"
// CHECK: rust.mir.aggregate
// CHECK: rust.mir.aggregate_tuple
// CHECK: rust.mir.copy
// CHECK: rust.mir.return
// CHECK: rust.mir.func
// CHECK-SAME: rust_name = "aggregate::make_pair"
// CHECK: rust.mir.local
// CHECK-SAME: rust_type = !rust.mir.adt
// CHECK: rust.mir.aggregate
// CHECK: rust.mir.aggregate_adt
// CHECK: rust.mir.return
