// RUN: rust-opt --sroa %s | FileCheck %s

module {
  rust.typed.func @array_slot {
    %slot = rust.typed.local_slot {index = 1 : i64, name = "_1"} : <!rust.typed.array<!rust.mir.int<"i32">, 2>>
    %a = rust.typed.const {debug = "11"} : !rust.mir.int<"i32">
    %b = rust.typed.const {debug = "22"} : !rust.mir.int<"i32">
    %array = rust.typed.aggregate %a, %b : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.typed.array<!rust.mir.int<"i32">, 2>
    rust.typed.store %array, %slot : !rust.typed.array<!rust.mir.int<"i32">, 2>, !rust.typed.slot<!rust.typed.array<!rust.mir.int<"i32">, 2>>
    %loaded = rust.typed.load %slot : !rust.typed.slot<!rust.typed.array<!rust.mir.int<"i32">, 2>> -> !rust.typed.array<!rust.mir.int<"i32">, 2>
    rust.typed.return %loaded : !rust.typed.array<!rust.mir.int<"i32">, 2>
  }
}

// CHECK-LABEL: rust.typed.func @array_slot
// CHECK-NOT: !rust.typed.array
// CHECK: %[[SLOT0:.*]] = rust.typed.local_slot
// CHECK-SAME: name = "_1.0"
// CHECK-SAME: <!rust.mir.int<"i32">>
// CHECK: %[[SLOT1:.*]] = rust.typed.local_slot
// CHECK-SAME: name = "_1.1"
// CHECK-SAME: <!rust.mir.int<"i32">>
// CHECK: rust.typed.field
// CHECK: rust.typed.store {{.*}}, %[[SLOT0]]
// CHECK: rust.typed.field
// CHECK: rust.typed.store {{.*}}, %[[SLOT1]]
// CHECK: rust.typed.load %[[SLOT0]]
// CHECK: rust.typed.load %[[SLOT1]]
// CHECK: rust.typed.aggregate
