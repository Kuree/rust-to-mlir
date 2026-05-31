// RUN: %rust_mir_extract --crate-root %S/../Inputs/float_cmp_math.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir=erase-source-mir --convert-rust-typed-to-arith --convert-rust-typed-to-func --convert-rust-typed-to-cf --mem2reg | FileCheck %s

// CHECK-LABEL: func.func @"float_cmp_math::eq_f64_typed"
// CHECK: arith.cmpf oeq, {{.*}} : f64
// CHECK-NOT: rust.typed.eq

// CHECK-LABEL: func.func @"float_cmp_math::ne_f64_typed"
// CHECK: arith.cmpf une, {{.*}} : f64
// CHECK-NOT: rust.typed.ne

// CHECK-LABEL: func.func @"float_cmp_math::lt_f64_typed"
// CHECK: arith.cmpf olt, {{.*}} : f64
// CHECK-NOT: rust.typed.lt

// CHECK-LABEL: func.func @"float_cmp_math::le_f64_typed"
// CHECK: arith.cmpf ole, {{.*}} : f64
// CHECK-NOT: rust.typed.le

// CHECK-LABEL: func.func @"float_cmp_math::gt_f64_typed"
// CHECK: arith.cmpf ogt, {{.*}} : f64
// CHECK-NOT: rust.typed.gt

// CHECK-LABEL: func.func @"float_cmp_math::ge_f64_typed"
// CHECK: arith.cmpf oge, {{.*}} : f64
// CHECK-NOT: rust.typed.ge

// CHECK-LABEL: func.func @"float_cmp_math::sqrt_f64_typed"
// CHECK: math.sqrt {{.*}} : f64
// CHECK-NOT: rust.typed.call

// CHECK-LABEL: func.func @"float_cmp_math::floor_f64_typed"
// CHECK: math.floor {{.*}} : f64
// CHECK-NOT: rust.typed.call

// CHECK-LABEL: func.func @"float_cmp_math::ceil_f64_typed"
// CHECK: math.ceil {{.*}} : f64
// CHECK-NOT: rust.typed.call

// CHECK-LABEL: func.func @"float_cmp_math::round_f64_typed"
// CHECK: math.round {{.*}} : f64
// CHECK-NOT: rust.typed.call

// CHECK-LABEL: func.func @"float_cmp_math::trunc_f64_typed"
// CHECK: math.trunc {{.*}} : f64
// CHECK-NOT: rust.typed.call

// CHECK-LABEL: func.func @"float_cmp_math::abs_f64_typed"
// CHECK: math.absf {{.*}} : f64
// CHECK-NOT: rust.typed.call

// CHECK-LABEL: func.func @"float_cmp_math::powi_f64_typed"
// CHECK: math.fpowi {{.*}} : f64, i32
// CHECK-NOT: rust.typed.call

