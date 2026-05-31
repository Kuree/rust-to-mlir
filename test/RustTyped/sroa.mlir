// RUN: rust-opt --sroa %s | FileCheck %s

module {
  rust.typed.func @scalar_slot {
    %slot = rust.typed.local_slot {index = 0 : i64, name = "_0", role = #rust.local_role<return>} : <!rust.mir.int<"i32">>
    %value = rust.typed.const {debug = "7"} : !rust.mir.int<"i32">
    rust.typed.store %value, %slot : !rust.mir.int<"i32">, !rust.typed.slot<!rust.mir.int<"i32">>
    %loaded = rust.typed.load %slot : !rust.typed.slot<!rust.mir.int<"i32">> -> !rust.mir.int<"i32">
    rust.typed.return %loaded : !rust.mir.int<"i32">
  }

  rust.typed.func @tuple_slot {
    %slot = rust.typed.local_slot {index = 1 : i64, name = "_1"} : <!rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.int<"i64">>>
    %a = rust.typed.const {debug = "11"} : !rust.mir.int<"i32">
    %b = rust.typed.const {debug = "22"} : !rust.mir.int<"i64">
    %tuple = rust.typed.aggregate %a, %b : !rust.mir.int<"i32">, !rust.mir.int<"i64"> -> !rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.int<"i64">>
    rust.typed.store %tuple, %slot : !rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.int<"i64">>, !rust.typed.slot<!rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.int<"i64">>>
    %loaded = rust.typed.load %slot : !rust.typed.slot<!rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.int<"i64">>> -> !rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.int<"i64">>
    rust.typed.return %loaded : !rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.int<"i64">>
  }
}

// CHECK-LABEL: rust.typed.func @scalar_slot
// CHECK: %[[SLOT:.*]] = rust.typed.local_slot
// CHECK-SAME: <!rust.mir.int<"i32">>
// CHECK: rust.typed.store
// CHECK: %[[LOADED:.*]] = rust.typed.load %[[SLOT]]
// CHECK: rust.typed.return %[[LOADED]]

// CHECK-LABEL: rust.typed.func @tuple_slot
// CHECK-NOT: !rust.typed.tuple
// CHECK: %[[SLOT0:.*]] = rust.typed.local_slot
// CHECK-SAME: name = "_1.0"
// CHECK-SAME: <!rust.mir.int<"i32">>
// CHECK: %[[SLOT1:.*]] = rust.typed.local_slot
// CHECK-SAME: name = "_1.1"
// CHECK-SAME: <!rust.mir.int<"i64">>
// CHECK: rust.typed.field
// CHECK: rust.typed.store {{.*}}, %[[SLOT0]]
// CHECK: rust.typed.field
// CHECK: rust.typed.store {{.*}}, %[[SLOT1]]
// CHECK: rust.typed.load %[[SLOT0]]
// CHECK: rust.typed.load %[[SLOT1]]
// CHECK: rust.typed.aggregate
