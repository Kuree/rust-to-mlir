// RUN: rust-opt --sroa --mem2reg %s | FileCheck %s

module {
  rust.typed.func @tuple_promote {
    %slot = rust.typed.local_slot {index = 0 : i64, name = "_0"} : <!rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.int<"i64">>>
    %a = rust.typed.const {debug = "11"} : !rust.mir.int<"i32">
    %b = rust.typed.const {debug = "22"} : !rust.mir.int<"i64">
    %tuple = rust.typed.aggregate %a, %b : !rust.mir.int<"i32">, !rust.mir.int<"i64"> -> !rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.int<"i64">>
    rust.typed.store %tuple, %slot : !rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.int<"i64">>, <!rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.int<"i64">>>
    %loaded = rust.typed.load %slot : <!rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.int<"i64">>> -> !rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.int<"i64">>
    rust.typed.return %loaded : !rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.int<"i64">>
  }
}

// CHECK-LABEL: rust.typed.func @tuple_promote
// CHECK-NOT: rust.typed.local_slot
// CHECK-NOT: rust.typed.store
// CHECK-NOT: rust.typed.load
// CHECK: %[[A:.*]] = rust.typed.const
// CHECK-SAME: debug = "11"
// CHECK: %[[B:.*]] = rust.typed.const
// CHECK-SAME: debug = "22"
// CHECK: %[[TUPLE:.*]] = rust.typed.aggregate %[[A]], %[[B]]
// CHECK: %[[FIELD0:.*]] = rust.typed.field %[[TUPLE]]
// CHECK-SAME: index = 0 : i64
// CHECK: %[[FIELD1:.*]] = rust.typed.field %[[TUPLE]]
// CHECK-SAME: index = 1 : i64
// CHECK: rust.typed.aggregate %[[FIELD0]], %[[FIELD1]]
// CHECK-NOT: rust.typed.local_slot
// CHECK-NOT: rust.typed.store
// CHECK-NOT: rust.typed.load
