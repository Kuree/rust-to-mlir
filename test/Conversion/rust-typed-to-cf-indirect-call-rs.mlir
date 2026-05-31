// RUN: %rust_mir_extract --crate-root %S/../Inputs/indirect_call.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir=erase-source-mir --convert-rust-typed-to-arith --convert-rust-typed-to-func --convert-rust-typed-to-cf --mem2reg | FileCheck %s

// CHECK-LABEL: func.func @"indirect_call::call_fn_ptr_typed"
// CHECK-SAME: (%[[F:.*]]: (i32) -> i32, %[[X:.*]]: i32) -> i32
// CHECK: %[[ARG:.*]] = rust.typed.load
// CHECK: %[[CALL:.*]] = call_indirect %[[F]](%[[ARG]])
// CHECK-SAME: (i32) -> i32
// CHECK: return %[[CALL]] : i32

// CHECK-LABEL: func.func @"indirect_call::choose_and_call_typed"
// CHECK: %[[INC:.*]] = constant {{.*}}@"indirect_call::inc_typed" : (i32) -> i32
// CHECK: %[[DOUBLE:.*]] = constant {{.*}}@"indirect_call::double_typed" : (i32) -> i32
// CHECK: ^bb{{.*}}(%[[FARG:.*]]: (i32) -> i32):
// CHECK: call_indirect %[[FARG]]
