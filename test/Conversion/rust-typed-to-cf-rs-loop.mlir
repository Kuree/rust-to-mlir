// RUN: %rust_mir_extract --crate-root %S/../Inputs/loop.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir --convert-rust-typed-to-arith --convert-rust-typed-to-func --convert-rust-typed-to-cf --mem2reg | FileCheck %s

// CHECK-LABEL: func.func @"loop::sum_to_typed"(%arg0: i32) -> i32
// CHECK: cf.br ^bb{{[0-9]+}}(%{{.*}}, %{{.*}} : i32, i32)
// CHECK: ^bb{{[0-9]+}}(%{{.*}}: i32, %{{.*}}: i32):
// CHECK: arith.cmpi slt
// CHECK: cf.switch
// CHECK: arith.addi
// CHECK: arith.addi
// CHECK: cf.br ^bb{{[0-9]+}}(%{{.*}}, %{{.*}} : i32, i32)
// CHECK: return {{.*}} : i32
// CHECK-NOT: rust.typed.switch_int
// CHECK-NOT: rust.typed.goto
