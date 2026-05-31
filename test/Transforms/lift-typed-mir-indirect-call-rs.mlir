// RUN: %rust_mir_extract --crate-root %S/../Inputs/indirect_call.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir | FileCheck %s

// CHECK-LABEL: rust.typed.func @"indirect_call::call_fn_ptr_typed"
// CHECK: rust.typed.call_indirect
// CHECK-SAME: (!rust.mir.int<"i32">) -> !rust.mir.int<"i32">

// CHECK-LABEL: rust.typed.func @"indirect_call::choose_and_call_typed"
// CHECK: rust.typed.fn_addr @"indirect_call::inc_typed"
// CHECK: rust.typed.fn_addr @"indirect_call::double_typed"
// CHECK: rust.typed.call_indirect
