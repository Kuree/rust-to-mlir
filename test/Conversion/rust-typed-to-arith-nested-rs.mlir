// RUN: %rust_mir_extract --crate-root %S/../Inputs/nested_binop.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir --convert-rust-typed-to-arith --convert-rust-typed-to-func --convert-rust-typed-to-cf --mem2reg | FileCheck %s

// CHECK-LABEL: func.func @"nested_binop::nested_typed"(%arg0: i64, %arg1: i64, %arg2: i64) -> i64
// CHECK-COUNT-2: arith.andi
// CHECK: return {{.*}} : i64
// CHECK-NOT: rust.typed.binop
