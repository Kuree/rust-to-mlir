// RUN: %rust_mir_extract --crate-root %S/../Inputs/call.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir --convert-rust-typed-to-arith --convert-rust-typed-to-func --convert-rust-typed-to-cf --mem2reg | FileCheck %s

// CHECK-LABEL: func.func @"call::caller_typed"
// CHECK-SAME: (%{{.*}}: i32) -> i32
// CHECK: %[[ARG:.*]] = rust.typed.load
// CHECK: %[[CALL:.*]] = call @"call::inc_typed"(%[[ARG]])
// CHECK-SAME: rust.abi = #rust.abi<rust>
// CHECK-SAME: rust.rust_name = "call::inc"
// CHECK-SAME: rust.target = 1 : i64
// CHECK-SAME: rust.unwind = #rust.unwind<Continue>
// CHECK-SAME: (i32) -> i32
// CHECK: return %[[CALL]] : i32
