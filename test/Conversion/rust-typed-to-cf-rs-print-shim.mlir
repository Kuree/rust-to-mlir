// RUN: %rust_mir_extract --crate-root %S/../Inputs/print_shim.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir --convert-rust-typed-to-arith --convert-rust-typed-to-func --convert-rust-typed-to-cf --mem2reg | FileCheck %s

// CHECK: func.func private @__rust_to_llvm_print_i32(i32)
// CHECK-SAME: llvm.linkage = #llvm.linkage<external>
// CHECK-SAME: rust.abi = #rust.abi<c>
// CHECK-SAME: rust.rust_name = "print_shim::__rust_to_llvm_print_i32"

// CHECK-LABEL: func.func @"print_shim::print_sum_typed"()
// CHECK: %[[ONE:.*]] = arith.constant 1 : i32
// CHECK: %[[TWO:.*]] = arith.constant 2 : i32
// CHECK: %[[SUM:.*]] = arith.addi %[[ONE]], %[[TWO]] : i32
// CHECK: cf.assert
// CHECK: %[[PRINT_VALUE:.*]] = llvm.extractvalue %{{.*}}[0] : !llvm.struct<(i32, i1)>
// CHECK: call @__rust_to_llvm_print_i32(%[[PRINT_VALUE]])
// CHECK-SAME: rust.abi = #rust.abi<c>
// CHECK-SAME: rust.rust_name = "print_shim::__rust_to_llvm_print_i32"
// CHECK-SAME: rust.target = 2 : i64
// CHECK-SAME: rust.unwind = #rust.unwind<Unreachable>
// CHECK-SAME: (i32) -> ()
// CHECK: return
// CHECK-NOT: call @"print_shim::__rust_to_llvm_print_i32_typed"
