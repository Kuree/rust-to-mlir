// RUN: %rust_mir_extract --crate-root %S/../Inputs/float_arith.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir=erase-source-mir --convert-rust-typed-to-arith --convert-rust-typed-to-func --convert-rust-typed-to-cf --mem2reg | FileCheck %s

// CHECK-LABEL: func.func @"float_arith::add_f32_typed"
// CHECK: arith.addf {{.*}} : f32
// CHECK-NOT: rust.typed.add

// CHECK-LABEL: func.func @"float_arith::neg_f32_typed"
// CHECK: arith.negf {{.*}} : f32
// CHECK-NOT: rust.typed.neg

// CHECK-LABEL: func.func @"float_arith::rem_f32_typed"
// CHECK: arith.remf {{.*}} : f32
// CHECK-NOT: rust.typed.rem

// CHECK-LABEL: func.func @"float_arith::affine_f64_typed"
// CHECK: arith.addf {{.*}} : f64
// CHECK: arith.mulf {{.*}} : f64
// CHECK: arith.subf {{.*}} : f64
// CHECK-NOT: rust.typed

// CHECK-LABEL: func.func @"float_arith::div_f64_typed"
// CHECK: arith.divf {{.*}} : f64
// CHECK-NOT: rust.typed.div
