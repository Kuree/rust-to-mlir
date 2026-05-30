// RUN: rust-opt --convert-rust-typed-memory-to-llvm %s | FileCheck %s

module {
  func.func @memory(%arg: !rust.typed.ref<"shared", i32>) -> !rust.typed.rawptr<"const", i32> {
    %slot = rust.typed.local_slot {address_taken = true, index = 0 : i64, name = "_0"} : <i32>
    %refslot = rust.typed.local_slot {index = 1 : i64, name = "_1"} : <!rust.typed.ref<"shared", i32>>
    %tuple_slot = rust.typed.local_slot {address_taken = true, index = 2 : i64, name = "_2"} : <!rust.typed.tuple<i32, i32>>
    %value = llvm.mlir.constant(7 : i32) : i32
    rust.typed.store %value, %slot : i32, <i32>
    %loaded = rust.typed.load %slot : <i32> -> i32
    %ref = rust.typed.borrow %slot {borrow_kind = #rust.borrow_kind<Shared>, mutability = #rust.mutability<shared>} : !rust.typed.slot<i32> -> !rust.typed.ref<"shared", i32>
    rust.typed.store %ref, %refslot : !rust.typed.ref<"shared", i32>, <!rust.typed.ref<"shared", i32>>
    %loaded_ref = rust.typed.load %refslot : <!rust.typed.ref<"shared", i32>> -> !rust.typed.ref<"shared", i32>
    func.call @consume_ref(%loaded_ref) : (!rust.typed.ref<"shared", i32>) -> ()
    %field = rust.typed.field_addr %tuple_slot {index = 1 : i64} : !rust.typed.slot<!rust.typed.tuple<i32, i32>> -> <i32>
    %field_ref = rust.typed.borrow %field {borrow_kind = #rust.borrow_kind<Shared>, mutability = #rust.mutability<shared>} : !rust.typed.addr<i32> -> !rust.typed.ref<"shared", i32>
    func.call @consume_ref(%field_ref) : (!rust.typed.ref<"shared", i32>) -> ()
    %raw = rust.typed.address_of %slot {mutability = #rust.mutability<const>, raw_ptr_kind = #rust.raw_ptr_kind<Const>} : !rust.typed.slot<i32> -> !rust.typed.rawptr<"const", i32>
    func.return %raw : !rust.typed.rawptr<"const", i32>
  }

  func.func private @consume_ref(!rust.typed.ref<"shared", i32>)
}

// CHECK-LABEL: func.func @memory(%{{.*}}: !llvm.ptr) -> !llvm.ptr
// CHECK: %[[ONE0:.*]] = llvm.mlir.constant(1 : i64) : i64
// CHECK: %[[SLOT:.*]] = llvm.alloca %[[ONE0]] x i32 : (i64) -> !llvm.ptr
// CHECK: %[[ONE1:.*]] = llvm.mlir.constant(1 : i64) : i64
// CHECK: %[[REFSLOT:.*]] = llvm.alloca %[[ONE1]] x !llvm.ptr : (i64) -> !llvm.ptr
// CHECK: %[[ONE2:.*]] = llvm.mlir.constant(1 : i64) : i64
// CHECK: %[[TUPLESLOT:.*]] = llvm.alloca %[[ONE2]] x !llvm.struct<(i32, i32)> : (i64) -> !llvm.ptr
// CHECK: llvm.store {{.*}}, %[[SLOT]] : i32, !llvm.ptr
// CHECK: %[[LOADED:.*]] = llvm.load %[[SLOT]] : !llvm.ptr -> i32
// CHECK: llvm.store %[[SLOT]], %[[REFSLOT]] : !llvm.ptr, !llvm.ptr
// CHECK: %[[LOADED_REF:.*]] = llvm.load %[[REFSLOT]] : !llvm.ptr -> !llvm.ptr
// CHECK: call @consume_ref(%[[LOADED_REF]]) : (!llvm.ptr) -> ()
// CHECK: %[[FIELD:.*]] = llvm.getelementptr %[[TUPLESLOT]][0, 1] : (!llvm.ptr) -> !llvm.ptr, !llvm.struct<(i32, i32)>
// CHECK: call @consume_ref(%[[FIELD]]) : (!llvm.ptr) -> ()
// CHECK: return %[[SLOT]] : !llvm.ptr
// CHECK-NOT: rust.typed.local_slot
// CHECK-NOT: rust.typed.field_addr
// CHECK-NOT: rust.typed.borrow
// CHECK-NOT: rust.typed.address_of

// CHECK-LABEL: func.func private @consume_ref(!llvm.ptr)
