// RUN: %rust_mir_extract --crate-root %S/../Inputs/cast_float.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir=erase-source-mir | FileCheck %s

// CHECK-LABEL: rust.typed.func @"cast_float::widen_float_typed"
// CHECK: rust.typed.numeric_cast {{.*}}cast_kind = #rust.cast_kind<FloatToFloat>{{.*}} : !rust.mir.float<32> -> !rust.mir.float<64>

// CHECK-LABEL: rust.typed.func @"cast_float::truncate_float_typed"
// CHECK: rust.typed.numeric_cast {{.*}}cast_kind = #rust.cast_kind<FloatToFloat>{{.*}} : !rust.mir.float<64> -> !rust.mir.float<32>

// CHECK-LABEL: rust.typed.func @"cast_float::signed_int_to_float_typed"
// CHECK: rust.typed.numeric_cast {{.*}}cast_kind = #rust.cast_kind<IntToFloat>{{.*}} : !rust.mir.int<"i32"> -> !rust.mir.float<32>

// CHECK-LABEL: rust.typed.func @"cast_float::unsigned_int_to_float_typed"
// CHECK: rust.typed.numeric_cast {{.*}}cast_kind = #rust.cast_kind<IntToFloat>{{.*}} : !rust.mir.int<"u32"> -> !rust.mir.float<64>

// CHECK-LABEL: rust.typed.func @"cast_float::float_to_signed_int_typed"
// CHECK: rust.typed.numeric_cast {{.*}}cast_kind = #rust.cast_kind<FloatToInt>{{.*}} : !rust.mir.float<32> -> !rust.mir.int<"i32">

// CHECK-LABEL: rust.typed.func @"cast_float::float_to_unsigned_int_typed"
// CHECK: rust.typed.numeric_cast {{.*}}cast_kind = #rust.cast_kind<FloatToInt>{{.*}} : !rust.mir.float<64> -> !rust.mir.int<"u32">
