// RUN: %rust_mir_extract --crate-root %S/../Inputs/add_locations.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir --convert-rust-typed-to-arith --convert-rust-typed-to-func --convert-rust-typed-to-cf --mem2reg | FileCheck %s

// CHECK-LABEL: func.func @"add_locations::add_typed"(%arg0: i32, %arg1: i32) -> i32
// CHECK: arith.addi
// CHECK: rust.typed.aggregate
// CHECK: rust.typed.field
// CHECK: return {{.*}} : i32
