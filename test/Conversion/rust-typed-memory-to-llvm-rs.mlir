// RUN: %rust_mir_extract --crate-root %S/../Inputs/references.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir --convert-rust-typed-to-arith --convert-rust-typed-to-func --convert-rust-typed-to-cf --sroa --mem2reg --convert-rust-typed-memory-to-llvm | FileCheck %s

// CHECK: func.func private @consume_raw(!llvm.ptr)
// CHECK: func.func private @consume_ref(!llvm.ptr)

// CHECK-LABEL: func.func @"references::borrow_i32_typed"
// CHECK: llvm.alloca
// CHECK: llvm.store
// CHECK: call @consume_ref({{.*}}) : (!llvm.ptr) -> ()
// CHECK-NOT: rust.typed.

// CHECK-LABEL: func.func @"references::raw_ptr_i32_typed"
// CHECK: llvm.alloca
// CHECK: llvm.store
// CHECK: call @consume_raw({{.*}}) : (!llvm.ptr) -> ()
// CHECK-NOT: rust.typed.

// CHECK-LABEL: func.func @"references::borrow_tuple_field_typed"
// CHECK: %[[PAIR:.*]] = llvm.alloca {{.*}} x !llvm.struct<(i32, i32)> : (i64) -> !llvm.ptr
// CHECK: llvm.store {{.*}}, %[[PAIR]] : !llvm.struct<(i32, i32)>, !llvm.ptr
// CHECK: %[[FIELD:.*]] = llvm.getelementptr %[[PAIR]][0, 1] : (!llvm.ptr) -> !llvm.ptr, !llvm.struct<(i32, i32)>
// CHECK: call @consume_ref(%[[FIELD]]) : (!llvm.ptr) -> ()
// CHECK-NOT: rust.typed.

// CHECK-LABEL: func.func @"references::raw_ptr_tuple_field_typed"
// CHECK: %[[RAW_PAIR:.*]] = llvm.alloca {{.*}} x !llvm.struct<(i32, i32)> : (i64) -> !llvm.ptr
// CHECK: llvm.store {{.*}}, %[[RAW_PAIR]] : !llvm.struct<(i32, i32)>, !llvm.ptr
// CHECK: %[[RAW_FIELD:.*]] = llvm.getelementptr %[[RAW_PAIR]][0, 1] : (!llvm.ptr) -> !llvm.ptr, !llvm.struct<(i32, i32)>
// CHECK: call @consume_raw(%[[RAW_FIELD]]) : (!llvm.ptr) -> ()
// CHECK-NOT: rust.typed.
