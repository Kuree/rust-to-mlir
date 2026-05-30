// RUN: rust-opt %s | FileCheck %s

module {
  rust.typed.func @typed {
    %slot = rust.typed.local_slot {index = 0 : i64, name = "_0", role = "return"} : <!rust.mir.int<"i32">>
    %value = rust.typed.const {debug = "7"} : !rust.mir.int<"i32">
    rust.typed.store %value, %slot : !rust.mir.int<"i32">, <!rust.mir.int<"i32">>
    %loaded = rust.typed.load %slot : <!rust.mir.int<"i32">> -> !rust.mir.int<"i32">
    rust.typed.return %loaded : !rust.mir.int<"i32">
  }
}

// CHECK: rust.typed.func @typed
// CHECK: rust.typed.local_slot
// CHECK-SAME: <!rust.mir.int<"i32">>
// CHECK: rust.typed.store
// CHECK: rust.typed.load
// CHECK: rust.typed.return
