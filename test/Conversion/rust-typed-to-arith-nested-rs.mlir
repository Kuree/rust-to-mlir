// RUN: rust-translate --rs %S/../Inputs/nested_binop.rs --rust-mir-extract %rust_mir_extract | rust-opt --rust-lift-typed-mir --convert-rust-typed-to-arith --convert-rust-typed-to-func --convert-rust-typed-to-cf --mem2reg | FileCheck %s

// CHECK-LABEL: func.func @"nested_binop::nested_typed"() -> i64
// CHECK-COUNT-2: arith.andi
// CHECK: return {{.*}} : i64
// CHECK-NOT: rust.typed.binop
