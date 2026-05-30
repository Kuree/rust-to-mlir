// RUN: %rust_mir_extract --crate-root %S/../Inputs/references.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir | FileCheck %s

// CHECK-LABEL: rust.typed.func @"references::borrow_i32_typed"
// CHECK: rust.typed.local_slot {{.*}}address_taken = true
// CHECK: !rust.typed.ref<"shared", !rust.mir.int<"i32">>
// CHECK: rust.typed.borrow
// CHECK-SAME: borrow_kind = "Shared"
// CHECK-SAME: mutability = "shared"
// CHECK-SAME: -> !rust.typed.ref<"shared", !rust.mir.int<"i32">>
// CHECK: rust.typed.call @consume_ref
// CHECK-SAME: (!rust.typed.ref<"shared", !rust.mir.int<"i32">>) -> ()

// CHECK-LABEL: rust.typed.func @"references::raw_ptr_i32_typed"
// CHECK: rust.typed.local_slot {{.*}}address_taken = true
// CHECK: !rust.typed.rawptr<"const", !rust.mir.int<"i32">>
// CHECK: rust.typed.address_of
// CHECK-SAME: mutability = "const"
// CHECK-SAME: raw_ptr_kind = "Const"
// CHECK-SAME: -> !rust.typed.rawptr<"const", !rust.mir.int<"i32">>
// CHECK: rust.typed.call @consume_raw
// CHECK-SAME: (!rust.typed.rawptr<"const", !rust.mir.int<"i32">>) -> ()
