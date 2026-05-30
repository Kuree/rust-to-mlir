// RUN: %rust_mir_extract --crate-root %S/../Inputs/print_shim.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir | FileCheck %s

// CHECK-LABEL: rust.typed.func @"print_shim::print_sum_typed"
// CHECK: rust.typed.call @__rust_to_llvm_print_i32(%{{[^)]*}})
// CHECK-SAME: abi = "c"
// CHECK-SAME: c_variadic = false
// CHECK-SAME: rust_name = "print_shim::__rust_to_llvm_print_i32"
// CHECK-SAME: target = 2 : i64
// CHECK-SAME: unwind = "Unreachable"
// CHECK-SAME: (!rust.mir.int<"i32">) -> ()
// CHECK: rust.typed.return
