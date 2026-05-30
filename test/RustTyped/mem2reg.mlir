// RUN: rust-opt --mem2reg %s | FileCheck %s

module {
  rust.typed.func @stored {
    %slot = rust.typed.local_slot {index = 0 : i64, name = "_0", role = #rust.local_role<return>} : <!rust.mir.int<"i32">>
    %value = rust.typed.const {debug = "7"} : !rust.mir.int<"i32">
    rust.typed.store %value, %slot : !rust.mir.int<"i32">, <!rust.mir.int<"i32">>
    %loaded = rust.typed.load %slot : <!rust.mir.int<"i32">> -> !rust.mir.int<"i32">
    rust.typed.return %loaded : !rust.mir.int<"i32">
  }

  rust.typed.func @default {
    %slot = rust.typed.local_slot {index = 1 : i64, name = "_1"} : <!rust.mir.int<"i32">>
    %loaded = rust.typed.load %slot : <!rust.mir.int<"i32">> -> !rust.mir.int<"i32">
    rust.typed.return %loaded : !rust.mir.int<"i32">
  }

  rust.typed.func @store_store_load {
    %slot = rust.typed.local_slot {index = 2 : i64, name = "_2"} : <!rust.mir.int<"i32">>
    %first = rust.typed.const {debug = "1"} : !rust.mir.int<"i32">
    %second = rust.typed.const {debug = "2"} : !rust.mir.int<"i32">
    rust.typed.store %first, %slot : !rust.mir.int<"i32">, <!rust.mir.int<"i32">>
    rust.typed.store %second, %slot : !rust.mir.int<"i32">, <!rust.mir.int<"i32">>
    %loaded = rust.typed.load %slot : <!rust.mir.int<"i32">> -> !rust.mir.int<"i32">
    rust.typed.return %loaded : !rust.mir.int<"i32">
  }

  rust.typed.func @arg_slot {
    %arg = rust.typed.local_slot {index = 1 : i64, name = "_1", role = #rust.local_role<arg>} : <!rust.mir.int<"i32">>
    %loaded = rust.typed.load %arg : <!rust.mir.int<"i32">> -> !rust.mir.int<"i32">
    rust.typed.return %loaded : !rust.mir.int<"i32">
  }
}

// CHECK-LABEL: rust.typed.func @stored
// CHECK: %[[VALUE:.*]] = rust.typed.const
// CHECK-SAME: debug = "7"
// CHECK-NOT: rust.typed.local_slot
// CHECK-NOT: rust.typed.store
// CHECK-NOT: rust.typed.load
// CHECK: rust.typed.return %[[VALUE]]

// CHECK-LABEL: rust.typed.func @default
// CHECK: %[[UNINIT:.*]] = rust.typed.const
// CHECK-SAME: debug = "uninit"
// CHECK-NOT: rust.typed.local_slot
// CHECK-NOT: rust.typed.store
// CHECK-NOT: rust.typed.load
// CHECK: rust.typed.return %[[UNINIT]]

// CHECK-LABEL: rust.typed.func @store_store_load
// CHECK: %[[FIRST:.*]] = rust.typed.const
// CHECK-SAME: debug = "1"
// CHECK: %[[SECOND:.*]] = rust.typed.const
// CHECK-SAME: debug = "2"
// CHECK-NOT: rust.typed.local_slot
// CHECK-NOT: rust.typed.store
// CHECK-NOT: rust.typed.load
// CHECK: rust.typed.return %[[SECOND]]
// CHECK-NOT: rust.typed.return %[[FIRST]]

// CHECK-LABEL: rust.typed.func @arg_slot
// CHECK: %[[ARG:.*]] = rust.typed.local_slot
// CHECK-SAME: role = #rust.local_role<arg>
// CHECK: %[[LOADED:.*]] = rust.typed.load %[[ARG]]
// CHECK: rust.typed.return %[[LOADED]]
