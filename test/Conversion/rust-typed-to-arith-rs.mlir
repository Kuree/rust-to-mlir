// RUN: %rust_mir_extract --crate-root %S/../Inputs/arith.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir --convert-rust-typed-to-arith --convert-rust-typed-to-func --convert-rust-typed-to-cf --mem2reg | FileCheck %s

// CHECK-LABEL: func.func @"arith::mask_typed"(%arg0: i64, %arg1: i64) -> i64
// CHECK: arith.andi
// CHECK: return {{.*}} : i64
// CHECK-NOT: rust.typed.bit_and
