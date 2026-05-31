// RUN: %rust_mir_extract --crate-root %S/../Inputs/float_arith.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir=erase-source-mir | FileCheck %s

// CHECK-LABEL: rust.typed.func @"float_arith::add_f32_typed"
// CHECK: rust.typed.load {{.*}} -> !rust.mir.float<32>
// CHECK: rust.typed.add {{.*}} : !rust.mir.float<32>, !rust.mir.float<32> -> !rust.mir.float<32>

// CHECK-LABEL: rust.typed.func @"float_arith::neg_f32_typed"
// CHECK: rust.typed.neg {{.*}} : !rust.mir.float<32> -> !rust.mir.float<32>

// CHECK-LABEL: rust.typed.func @"float_arith::rem_f32_typed"
// CHECK: rust.typed.rem {{.*}} : !rust.mir.float<32>, !rust.mir.float<32> -> !rust.mir.float<32>

// CHECK-LABEL: rust.typed.func @"float_arith::affine_f64_typed"
// CHECK: rust.typed.add {{.*}} : !rust.mir.float<64>, !rust.mir.float<64> -> !rust.mir.float<64>
// CHECK: rust.typed.mul {{.*}} : !rust.mir.float<64>, !rust.mir.float<64> -> !rust.mir.float<64>
// CHECK: rust.typed.sub {{.*}} : !rust.mir.float<64>, !rust.mir.float<64> -> !rust.mir.float<64>

// CHECK-LABEL: rust.typed.func @"float_arith::div_f64_typed"
// CHECK: rust.typed.div {{.*}} : !rust.mir.float<64>, !rust.mir.float<64> -> !rust.mir.float<64>
