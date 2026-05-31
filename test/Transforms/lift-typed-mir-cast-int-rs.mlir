// RUN: %rust_mir_extract --crate-root %S/../Inputs/cast_int.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir=erase-source-mir | FileCheck %s

// CHECK-LABEL: rust.typed.func @"cast_int::widen_signed_typed"
// CHECK: rust.typed.int_cast {{.*}} : !rust.mir.int<"i32"> -> !rust.mir.int<"i64">

// CHECK-LABEL: rust.typed.func @"cast_int::widen_unsigned_typed"
// CHECK: rust.typed.int_cast {{.*}} : !rust.mir.int<"u32"> -> !rust.mir.int<"u64">

// CHECK-LABEL: rust.typed.func @"cast_int::truncate_signed_typed"
// CHECK: rust.typed.int_cast {{.*}} : !rust.mir.int<"i64"> -> !rust.mir.int<"i32">

// CHECK-LABEL: rust.typed.func @"cast_int::same_width_signedness_typed"
// CHECK: rust.typed.int_cast {{.*}} : !rust.mir.int<"u32"> -> !rust.mir.int<"i32">
