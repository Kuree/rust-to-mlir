// RUN: %rust_mir_extract --crate-root %S/../Inputs/add_locations.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir --convert-rust-typed-to-arith --convert-rust-typed-to-func --convert-rust-typed-to-cf --mem2reg | FileCheck %s

// CHECK-LABEL: func.func @"add_locations::add_typed"(%arg0: i32, %arg1: i32) -> i32
// CHECK: %[[ZERO:.*]] = arith.constant 0 : i32
// CHECK: %[[VALUE:.*]] = arith.addi %{{.*}}, %{{.*}} : i32
// CHECK: %[[LHS_XOR_VALUE:.*]] = arith.xori %{{.*}}, %[[VALUE]] : i32
// CHECK: %[[RHS_XOR_VALUE:.*]] = arith.xori %{{.*}}, %[[VALUE]] : i32
// CHECK: %[[OVERFLOW_BITS:.*]] = arith.andi %[[LHS_XOR_VALUE]], %[[RHS_XOR_VALUE]] : i32
// CHECK: %[[OVERFLOW:.*]] = arith.cmpi slt, %[[OVERFLOW_BITS]], %[[ZERO]] : i32
// CHECK: %[[CHECK_TRUE:.*]] = arith.constant true
// CHECK: %[[CHECK_OK:.*]] = arith.xori %[[OVERFLOW]], %[[CHECK_TRUE]] : i1
// CHECK: cf.assert %[[CHECK_OK]], "attempt to add with overflow"
// CHECK: %[[UNDEF:.*]] = llvm.mlir.undef : !llvm.struct<(i32, i1)>
// CHECK: %[[WITH_VALUE:.*]] = llvm.insertvalue %[[VALUE]], %[[UNDEF]][0] : !llvm.struct<(i32, i1)>
// CHECK: %[[CHECKED:.*]] = llvm.insertvalue %[[OVERFLOW]], %[[WITH_VALUE]][1] : !llvm.struct<(i32, i1)>
// CHECK: rust.typed.store %[[CHECKED]], %[[CHECKED_SLOT:.*]] : !llvm.struct<(i32, i1)>, !rust.typed.slot<!llvm.struct<(i32, i1)>>
// CHECK: %[[ASSERT_FIELD:.*]] = rust.typed.field_addr %[[CHECKED_SLOT]] {index = 1 : i64} : !rust.typed.slot<!llvm.struct<(i32, i1)>> -> <i1>
// CHECK: %[[ASSERT_OVERFLOW:.*]] = rust.typed.load %[[ASSERT_FIELD]] : !rust.typed.addr<i1> -> i1
// CHECK: %[[ASSERT_TRUE:.*]] = arith.constant true
// CHECK: %[[ASSERT_OK:.*]] = arith.xori %[[ASSERT_OVERFLOW]], %[[ASSERT_TRUE]] : i1
// CHECK: cf.assert %[[ASSERT_OK]], "attempt to add with overflow"
// CHECK: %[[RETURN_FIELD:.*]] = rust.typed.field_addr %[[CHECKED_SLOT]] {index = 0 : i64} : !rust.typed.slot<!llvm.struct<(i32, i1)>> -> <i32>
// CHECK: %[[RETURN_VALUE:.*]] = rust.typed.load %[[RETURN_FIELD]] : !rust.typed.addr<i32> -> i32
// CHECK: return %[[RETURN_VALUE]] : i32
// CHECK-NOT: rust.typed.aggregate
// CHECK-NOT: rust.typed.field %
