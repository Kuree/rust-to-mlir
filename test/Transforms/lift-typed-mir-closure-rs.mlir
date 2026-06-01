// RUN: %rust_mir_extract --crate-root %S/../Inputs/closure.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir=erase-source-mir | FileCheck %s

// CHECK-LABEL: rust.typed.func @"closure::noncapturing_typed"
// CHECK: rust.typed.local_slot {{.*}} : <!rust.mir.unit>
// CHECK: rust.typed.call @"closure::noncapturing::{closure#0}_typed"
// CHECK-SAME: callee_def = "std::ops::Fn::call"
// CHECK-SAME: rust_name = "closure::noncapturing::{closure#0}"

// CHECK-LABEL: rust.typed.func @"closure::capture_copy_typed"
// CHECK: rust.typed.local_slot {{.*}} : <!rust.typed.tuple<!rust.typed.ref<shared, !rust.mir.int<"i32">>>>
// CHECK: rust.typed.aggregate {{.*}} -> !rust.typed.tuple<!rust.typed.ref<shared, !rust.mir.int<"i32">>>
// CHECK: rust.typed.call @"closure::capture_copy::{closure#0}_typed"
// CHECK-SAME: callee_def = "std::ops::Fn::call"
// CHECK-SAME: rust_name = "closure::capture_copy::{closure#0}"

// CHECK-LABEL: rust.typed.func @"closure::capture_move_typed"
// CHECK: rust.typed.local_slot {{.*}} : <!rust.typed.tuple<!rust.mir.int<"i32">>>
// CHECK: rust.typed.aggregate {{.*}} -> !rust.typed.tuple<!rust.mir.int<"i32">>
// CHECK: rust.typed.call @"closure::capture_move::{closure#0}_typed"
// CHECK-SAME: callee_def = "std::ops::Fn::call"
// CHECK-SAME: rust_name = "closure::capture_move::{closure#0}"

// CHECK-LABEL: rust.typed.func @"closure::capture_mut_typed"
// CHECK: rust.typed.local_slot {{.*}} : <!rust.typed.tuple<!rust.typed.ref<mut, !rust.mir.int<"i32">>>>
// CHECK: rust.typed.call @"closure::capture_mut::{closure#0}_typed"(%{{[^,)]*}})
// CHECK-SAME: callee_def = "std::ops::FnMut::call_mut"
// CHECK-SAME: rust_name = "closure::capture_mut::{closure#0}"
