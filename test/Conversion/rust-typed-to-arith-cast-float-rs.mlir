// RUN: %rust_mir_extract --crate-root %S/../Inputs/cast_float.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir=erase-source-mir --convert-rust-typed-to-arith --convert-rust-typed-to-func --convert-rust-typed-to-cf --mem2reg | FileCheck %s

// CHECK-LABEL: func.func @"cast_float::widen_float_typed"
// CHECK: arith.extf {{.*}} : f32 to f64
// CHECK-NOT: rust.typed.numeric_cast

// CHECK-LABEL: func.func @"cast_float::truncate_float_typed"
// CHECK: arith.truncf {{.*}} : f64 to f32
// CHECK-NOT: rust.typed.numeric_cast

// CHECK-LABEL: func.func @"cast_float::signed_int_to_float_typed"
// CHECK: arith.sitofp {{.*}} : i32 to f32
// CHECK-NOT: rust.typed.numeric_cast

// CHECK-LABEL: func.func @"cast_float::unsigned_int_to_float_typed"
// CHECK: arith.uitofp {{.*}} : i32 to f64
// CHECK-NOT: rust.typed.numeric_cast

// CHECK-LABEL: func.func @"cast_float::float_to_signed_int_typed"
// CHECK-DAG: arith.constant 0.000000e+00 : f32
// CHECK-DAG: arith.constant {{.*}} : f32
// CHECK: arith.fptosi {{.*}} : f32 to i32
// CHECK-NOT: rust.typed.numeric_cast

// CHECK-LABEL: func.func @"cast_float::float_to_unsigned_int_typed"
// CHECK-DAG: arith.constant 0.000000e+00 : f64
// CHECK-DAG: arith.constant {{.*}} : f64
// CHECK: arith.fptoui {{.*}} : f64 to i32
// CHECK-NOT: rust.typed.numeric_cast
