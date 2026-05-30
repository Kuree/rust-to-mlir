// RUN: %rust_mir_extract --crate-root %S/../Inputs/print_str_shim.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir --convert-rust-typed-to-arith --convert-rust-typed-to-func --convert-rust-typed-to-cf --mem2reg --convert-rust-typed-memory-to-llvm | FileCheck %s

// CHECK: llvm.mlir.global private constant @__rtl_str_10_

// CHECK-LABEL: func.func @"print_str_shim::print_str_typed"()
// CHECK-NOT: unrealized_conversion_cast
// CHECK: %[[PTR:.*]] = llvm.mlir.addressof @__rtl_str_10_
// CHECK-NOT: unrealized_conversion_cast
// CHECK: %[[LEN:.*]] = llvm.mlir.constant(10 : i64) : i64
// CHECK-NOT: unrealized_conversion_cast
// CHECK: call @__rtl_println_str(%[[PTR]], %[[LEN]])
// CHECK-NOT: unrealized_conversion_cast
