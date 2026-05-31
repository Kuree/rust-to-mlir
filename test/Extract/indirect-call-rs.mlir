// RUN: %rust_mir_extract --crate-root %S/../Inputs/indirect_call.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc | FileCheck %s

// CHECK-LABEL: rust.mir.func @"indirect_call::call_fn_ptr"
// CHECK: rust_type = (!rust.mir.int<"i32">) -> !rust.mir.int<"i32">
// CHECK: rust.mir.call
// CHECK: rust.mir.copy
// CHECK: rust.mir.place 1

// CHECK-LABEL: rust.mir.func @"indirect_call::choose_and_call"
// CHECK: rust.mir.cast
// CHECK-SAME: cast_kind = #rust.cast_kind<PointerCoercion>
// CHECK: rust.mir.call
