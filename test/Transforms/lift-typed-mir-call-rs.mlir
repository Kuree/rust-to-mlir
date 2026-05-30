// RUN: %rust_mir_extract --crate-root %S/../Inputs/call.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir | FileCheck %s

// CHECK-LABEL: rust.typed.func @"call::caller_typed"
// CHECK: rust.typed.block 0
// CHECK: %[[ARG:.*]] = rust.typed.load
// CHECK: %[[CALL:.*]] = rust.typed.call @"call::inc_typed"(%[[ARG]])
// CHECK-SAME: abi = "rust"
// CHECK-SAME: rust_name = "call::inc"
// CHECK-SAME: (!rust.mir.int<"i32">) -> !rust.mir.int<"i32">
// CHECK: rust.typed.store %[[CALL]]
// CHECK: rust.typed.goto
