// RUN: %rust_mir_extract --crate-root %S/../Inputs/references.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir | FileCheck %s

// CHECK-LABEL: rust.typed.func @"references::borrow_i32_typed"
// CHECK: rust.typed.local_slot {{.*}}address_taken = true
// CHECK: !rust.typed.ref<shared, !rust.mir.int<"i32">>
// CHECK: rust.typed.borrow
// CHECK-SAME: borrow_kind = #rust.borrow_kind<Shared>
// CHECK-SAME: mutability = #rust.mutability<shared>
// CHECK-SAME: -> !rust.typed.ref<shared, !rust.mir.int<"i32">>
// CHECK: rust.typed.call @consume_ref
// CHECK-SAME: (!rust.typed.ref<shared, !rust.mir.int<"i32">>) -> ()

// CHECK-LABEL: rust.typed.func @"references::raw_ptr_i32_typed"
// CHECK: rust.typed.local_slot {{.*}}address_taken = true
// CHECK: !rust.typed.rawptr<const, !rust.mir.int<"i32">>
// CHECK: rust.typed.address_of
// CHECK-SAME: mutability = #rust.mutability<const>
// CHECK-SAME: raw_ptr_kind = #rust.raw_ptr_kind<Const>
// CHECK-SAME: -> !rust.typed.rawptr<const, !rust.mir.int<"i32">>
// CHECK: rust.typed.call @consume_raw
// CHECK-SAME: (!rust.typed.rawptr<const, !rust.mir.int<"i32">>) -> ()

// CHECK-LABEL: rust.typed.func @"references::borrow_tuple_field_typed"
// CHECK: rust.typed.local_slot {{.*}}address_taken = true
// CHECK-SAME: !rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.int<"i32">>
// CHECK: %[[FIELD:.*]] = rust.typed.field_addr
// CHECK-SAME: index = 1 : i64
// CHECK-SAME: -> <!rust.mir.int<"i32">>
// CHECK: rust.typed.borrow %[[FIELD]]
// CHECK-SAME: !rust.typed.addr<!rust.mir.int<"i32">>
// CHECK-SAME: -> !rust.typed.ref<shared, !rust.mir.int<"i32">>
// CHECK: rust.typed.call @consume_ref
// CHECK-SAME: (!rust.typed.ref<shared, !rust.mir.int<"i32">>) -> ()

// CHECK-LABEL: rust.typed.func @"references::raw_ptr_tuple_field_typed"
// CHECK: rust.typed.local_slot {{.*}}address_taken = true
// CHECK-SAME: !rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.int<"i32">>
// CHECK: %[[RAW_FIELD:.*]] = rust.typed.field_addr
// CHECK-SAME: index = 1 : i64
// CHECK-SAME: -> <!rust.mir.int<"i32">>
// CHECK: rust.typed.address_of %[[RAW_FIELD]]
// CHECK-SAME: !rust.typed.addr<!rust.mir.int<"i32">>
// CHECK-SAME: -> !rust.typed.rawptr<const, !rust.mir.int<"i32">>
// CHECK: rust.typed.call @consume_raw
// CHECK-SAME: (!rust.typed.rawptr<const, !rust.mir.int<"i32">>) -> ()
