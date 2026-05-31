// RUN: rust-opt --mem2reg %s | FileCheck %s --check-prefix=MEM2REG
// RUN: rust-opt --sroa %s | FileCheck %s --check-prefix=SROA

module {
  rust.typed.func @address_taken_scalar {
    %slot = rust.typed.local_slot {address_taken = true, index = 0 : i64, name = "_0"} : <!rust.mir.int<"i32">>
    %value = rust.typed.const {debug = "7"} : !rust.mir.int<"i32">
    rust.typed.store %value, %slot : !rust.mir.int<"i32">, !rust.typed.slot<!rust.mir.int<"i32">>
    %loaded = rust.typed.load %slot : !rust.typed.slot<!rust.mir.int<"i32">> -> !rust.mir.int<"i32">
    rust.typed.return %loaded : !rust.mir.int<"i32">
  }

  rust.typed.func @address_taken_tuple {
    %slot = rust.typed.local_slot {address_taken = true, index = 1 : i64, name = "_1"} : <!rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.int<"i64">>>
    %a = rust.typed.const {debug = "11"} : !rust.mir.int<"i32">
    %b = rust.typed.const {debug = "22"} : !rust.mir.int<"i64">
    %tuple = rust.typed.aggregate %a, %b : !rust.mir.int<"i32">, !rust.mir.int<"i64"> -> !rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.int<"i64">>
    rust.typed.store %tuple, %slot : !rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.int<"i64">>, !rust.typed.slot<!rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.int<"i64">>>
    %loaded = rust.typed.load %slot : !rust.typed.slot<!rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.int<"i64">>> -> !rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.int<"i64">>
    rust.typed.return %loaded : !rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.int<"i64">>
  }
}

// MEM2REG-LABEL: rust.typed.func @address_taken_scalar
// MEM2REG: %[[SLOT:.*]] = rust.typed.local_slot
// MEM2REG-SAME: address_taken = true
// MEM2REG: rust.typed.store {{.*}}, %[[SLOT]]
// MEM2REG: %[[LOADED:.*]] = rust.typed.load %[[SLOT]]
// MEM2REG: rust.typed.return %[[LOADED]]

// SROA-LABEL: rust.typed.func @address_taken_tuple
// SROA-NOT: name = "_1.0"
// SROA-NOT: name = "_1.1"
// SROA: %[[SLOT:.*]] = rust.typed.local_slot
// SROA-SAME: address_taken = true
// SROA-SAME: <!rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.int<"i64">>>
// SROA: rust.typed.store {{.*}}, %[[SLOT]]
// SROA: %[[LOADED:.*]] = rust.typed.load %[[SLOT]]
// SROA: rust.typed.return %[[LOADED]]
