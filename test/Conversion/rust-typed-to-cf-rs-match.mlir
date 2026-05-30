// RUN: %rust_mir_extract --crate-root %S/../Inputs/match_int.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir --convert-rust-typed-to-arith --convert-rust-typed-to-func --convert-rust-typed-to-cf --mem2reg | FileCheck %s

// CHECK-LABEL: func.func @"match_int::classify_typed"(%arg0: i32) -> i32
// CHECK: cf.switch {{.*}} : i32, [
// CHECK: default:
// CHECK: 0:
// CHECK: 1:
// CHECK: 7:
// CHECK: arith.constant -1 : i32
// CHECK: arith.constant 70 : i32
// CHECK: arith.constant 20 : i32
// CHECK: arith.constant 10 : i32
// CHECK: return {{.*}} : i32
// CHECK-NOT: rust.typed.switch_int
// CHECK-NOT: rust.typed.goto
