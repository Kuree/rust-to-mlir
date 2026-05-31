// RUN: %rust_mir_extract --crate-root %S/../Inputs/cast_int.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir=erase-source-mir --convert-rust-typed-to-arith --convert-rust-typed-to-func --convert-rust-typed-to-cf --mem2reg | FileCheck %s

// CHECK-LABEL: func.func @"cast_int::widen_signed_typed"
// CHECK: arith.extsi {{.*}} : i32 to i64
// CHECK: return {{.*}} : i64
// CHECK-NOT: rust.typed.int_cast

// CHECK-LABEL: func.func @"cast_int::widen_unsigned_typed"
// CHECK: arith.extui {{.*}} : i32 to i64
// CHECK: return {{.*}} : i64
// CHECK-NOT: rust.typed.int_cast

// CHECK-LABEL: func.func @"cast_int::truncate_signed_typed"
// CHECK: arith.trunci {{.*}} : i64 to i32
// CHECK: return {{.*}} : i32
// CHECK-NOT: rust.typed.int_cast

// CHECK-LABEL: func.func @"cast_int::same_width_signedness_typed"
// CHECK-NOT: arith.ext
// CHECK-NOT: arith.trunci
// CHECK: return {{.*}} : i32
// CHECK-NOT: rust.typed.int_cast
