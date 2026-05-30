// RUN: %rust_mir_extract --crate-root %S/../Inputs/const_binop.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir --convert-rust-typed-to-arith --convert-rust-typed-to-func --convert-rust-typed-to-cf --mem2reg | FileCheck %s

// CHECK-LABEL: func.func @"const_binop::mask_const_typed"(%arg0: i64) -> i64
// CHECK: arith.constant 3 : i64
// CHECK: arith.andi
// CHECK: return {{.*}} : i64
// CHECK-NOT: rust.typed.const
