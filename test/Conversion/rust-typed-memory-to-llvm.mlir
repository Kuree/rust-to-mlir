// RUN: rust-opt --convert-rust-typed-memory-to-llvm %s | FileCheck %s

module {
  func.func @memory(%arg: !rust.typed.ref<"shared", i32>) -> !rust.typed.rawptr<"const", i32> {
    %slot = rust.typed.local_slot {address_taken = true, index = 0 : i64, name = "_0"} : <i32>
    %refslot = rust.typed.local_slot {index = 1 : i64, name = "_1"} : <!rust.typed.ref<"shared", i32>>
    %tuple_slot = rust.typed.local_slot {address_taken = true, index = 2 : i64, name = "_2"} : <!rust.typed.tuple<i32, i32>>
    %array_slot = rust.typed.local_slot {address_taken = true, index = 3 : i64, name = "_3"} : <!rust.typed.array<i32, 4>>
    %value = llvm.mlir.constant(7 : i32) : i32
    rust.typed.store %value, %slot : i32, !rust.typed.slot<i32>
    %loaded = rust.typed.load %slot : !rust.typed.slot<i32> -> i32
    %ref = rust.typed.borrow %slot {borrow_kind = #rust.borrow_kind<Shared>, mutability = #rust.mutability<shared>} : !rust.typed.slot<i32> -> !rust.typed.ref<"shared", i32>
    rust.typed.store %ref, %refslot : !rust.typed.ref<"shared", i32>, !rust.typed.slot<!rust.typed.ref<"shared", i32>>
    %loaded_ref = rust.typed.load %refslot : !rust.typed.slot<!rust.typed.ref<"shared", i32>> -> !rust.typed.ref<"shared", i32>
    func.call @consume_ref(%loaded_ref) : (!rust.typed.ref<"shared", i32>) -> ()
    %field = rust.typed.field_addr %tuple_slot {index = 1 : i64} : !rust.typed.slot<!rust.typed.tuple<i32, i32>> -> <i32>
    %field_ref = rust.typed.borrow %field {borrow_kind = #rust.borrow_kind<Shared>, mutability = #rust.mutability<shared>} : !rust.typed.addr<i32> -> !rust.typed.ref<"shared", i32>
    func.call @consume_ref(%field_ref) : (!rust.typed.ref<"shared", i32>) -> ()
    %index = llvm.mlir.constant(2 : i64) : i64
    %element = rust.typed.index_addr %array_slot[%index] : !rust.typed.slot<!rust.typed.array<i32, 4>>, i64 -> <i32>
    rust.typed.store %value, %element : i32, !rust.typed.addr<i32>
    %raw = rust.typed.address_of %slot {mutability = #rust.mutability<const>, raw_ptr_kind = #rust.raw_ptr_kind<Const>} : !rust.typed.slot<i32> -> !rust.typed.rawptr<"const", i32>
    func.return %raw : !rust.typed.rawptr<"const", i32>
  }

  func.func private @consume_ref(!rust.typed.ref<"shared", i32>)

  func.func @slice_memory() {
    %array_slot = rust.typed.local_slot {address_taken = true, index = 4 : i64, name = "_4"} : <!rust.typed.array<i32, 4>>
    %slice_slot = rust.typed.local_slot {index = 5 : i64, name = "_5"} : <!rust.typed.ref<"shared", !rust.typed.slice<i32>>>
    %array_ref = rust.typed.borrow %array_slot {borrow_kind = #rust.borrow_kind<Shared>, mutability = #rust.mutability<shared>} : !rust.typed.slot<!rust.typed.array<i32, 4>> -> !rust.typed.ref<"shared", !rust.typed.array<i32, 4>>
    %slice = rust.typed.slice_from_array %array_ref : !rust.typed.ref<"shared", !rust.typed.array<i32, 4>> -> !rust.typed.ref<"shared", !rust.typed.slice<i32>>
    rust.typed.store %slice, %slice_slot : !rust.typed.ref<"shared", !rust.typed.slice<i32>>, !rust.typed.slot<!rust.typed.ref<"shared", !rust.typed.slice<i32>>>
    %loaded_slice = rust.typed.load %slice_slot : !rust.typed.slot<!rust.typed.ref<"shared", !rust.typed.slice<i32>>> -> !rust.typed.ref<"shared", !rust.typed.slice<i32>>
    %len = rust.typed.ptr_metadata %loaded_slice : !rust.typed.ref<"shared", !rust.typed.slice<i32>> -> i64
    %index = llvm.mlir.constant(2 : i64) : i64
    %element = rust.typed.index_addr %loaded_slice[%index] : !rust.typed.ref<"shared", !rust.typed.slice<i32>>, i64 -> <i32>
    %value = rust.typed.load %element : !rust.typed.addr<i32> -> i32
    %sub = rust.typed.subslice %loaded_slice {from_end = true, from_index = 1 : i64, to_index = 1 : i64} : !rust.typed.ref<"shared", !rust.typed.slice<i32>> -> !rust.typed.ref<"shared", !rust.typed.slice<i32>>
    %sub_len = rust.typed.ptr_metadata %sub : !rust.typed.ref<"shared", !rust.typed.slice<i32>> -> i64
    func.call @consume_i64(%len) : (i64) -> ()
    func.call @consume_i64(%sub_len) : (i64) -> ()
    func.call @consume_i32(%value) : (i32) -> ()
    func.return
  }

  func.func private @consume_i64(i64)
  func.func private @consume_i32(i32)
}

// CHECK-LABEL: func.func @memory(%{{.*}}: !llvm.ptr) -> !llvm.ptr
// CHECK: %[[ONE0:.*]] = llvm.mlir.constant(1 : i64) : i64
// CHECK: %[[SLOT:.*]] = llvm.alloca %[[ONE0]] x i32 : (i64) -> !llvm.ptr
// CHECK: %[[ONE1:.*]] = llvm.mlir.constant(1 : i64) : i64
// CHECK: %[[REFSLOT:.*]] = llvm.alloca %[[ONE1]] x !llvm.ptr : (i64) -> !llvm.ptr
// CHECK: %[[ONE2:.*]] = llvm.mlir.constant(1 : i64) : i64
// CHECK: %[[TUPLESLOT:.*]] = llvm.alloca %[[ONE2]] x !llvm.struct<(i32, i32)> : (i64) -> !llvm.ptr
// CHECK: %[[ONE3:.*]] = llvm.mlir.constant(1 : i64) : i64
// CHECK: %[[ARRAYSLOT:.*]] = llvm.alloca %[[ONE3]] x !llvm.array<4 x i32> : (i64) -> !llvm.ptr
// CHECK: llvm.store {{.*}}, %[[SLOT]] : i32, !llvm.ptr
// CHECK: %[[LOADED:.*]] = llvm.load %[[SLOT]] : !llvm.ptr -> i32
// CHECK: llvm.store %[[SLOT]], %[[REFSLOT]] : !llvm.ptr, !llvm.ptr
// CHECK: %[[LOADED_REF:.*]] = llvm.load %[[REFSLOT]] : !llvm.ptr -> !llvm.ptr
// CHECK: call @consume_ref(%[[LOADED_REF]]) : (!llvm.ptr) -> ()
// CHECK: %[[FIELD:.*]] = llvm.getelementptr %[[TUPLESLOT]][0, 1] : (!llvm.ptr) -> !llvm.ptr, !llvm.struct<(i32, i32)>
// CHECK: call @consume_ref(%[[FIELD]]) : (!llvm.ptr) -> ()
// CHECK: %[[INDEX:.*]] = llvm.mlir.constant(2 : i64) : i64
// CHECK: %[[ELEMENT:.*]] = llvm.getelementptr %[[ARRAYSLOT]][0, %[[INDEX]]] : (!llvm.ptr, i64) -> !llvm.ptr, !llvm.array<4 x i32>
// CHECK: llvm.store {{.*}}, %[[ELEMENT]] : i32, !llvm.ptr
// CHECK: return %[[SLOT]] : !llvm.ptr
// CHECK-NOT: rust.typed.local_slot
// CHECK-NOT: rust.typed.field_addr
// CHECK-NOT: rust.typed.index_addr
// CHECK-NOT: rust.typed.borrow
// CHECK-NOT: rust.typed.address_of

// CHECK-LABEL: func.func private @consume_ref(!llvm.ptr)

// CHECK-LABEL: func.func @slice_memory()
// CHECK: %[[ARRAY_SLOT:.*]] = llvm.alloca {{.*}} x !llvm.array<4 x i32> : (i64) -> !llvm.ptr
// CHECK: %[[SLICE_SLOT:.*]] = llvm.alloca {{.*}} x !llvm.struct<(ptr, i64)> : (i64) -> !llvm.ptr
// CHECK: %[[DATA:.*]] = llvm.getelementptr %[[ARRAY_SLOT]][0, 0] : (!llvm.ptr) -> !llvm.ptr, !llvm.array<4 x i32>
// CHECK: %[[LEN:.*]] = llvm.mlir.constant(4 : i64) : i64
// CHECK: %[[FAT0:.*]] = llvm.mlir.undef : !llvm.struct<(ptr, i64)>
// CHECK: %[[FAT1:.*]] = llvm.insertvalue %[[DATA]], %[[FAT0]][0] : !llvm.struct<(ptr, i64)>
// CHECK: %[[FAT:.*]] = llvm.insertvalue %[[LEN]], %[[FAT1]][1] : !llvm.struct<(ptr, i64)>
// CHECK: llvm.store %[[FAT]], %[[SLICE_SLOT]] : !llvm.struct<(ptr, i64)>, !llvm.ptr
// CHECK: %[[LOADED_SLICE:.*]] = llvm.load %[[SLICE_SLOT]] : !llvm.ptr -> !llvm.struct<(ptr, i64)>
// CHECK: %[[SLICE_LEN:.*]] = llvm.extractvalue %[[LOADED_SLICE]][1] : !llvm.struct<(ptr, i64)>
// CHECK: %[[SLICE_DATA:.*]] = llvm.extractvalue %[[LOADED_SLICE]][0] : !llvm.struct<(ptr, i64)>
// CHECK: %[[ELEMENT_PTR:.*]] = llvm.getelementptr %[[SLICE_DATA]][{{.*}}] : (!llvm.ptr, i64) -> !llvm.ptr, i32
// CHECK: %[[VALUE:.*]] = llvm.load %[[ELEMENT_PTR]] : !llvm.ptr -> i32
// CHECK: %[[SUB_DATA_BASE:.*]] = llvm.extractvalue %[[LOADED_SLICE]][0] : !llvm.struct<(ptr, i64)>
// CHECK: %[[SUB_SOURCE_LEN:.*]] = llvm.extractvalue %[[LOADED_SLICE]][1] : !llvm.struct<(ptr, i64)>
// CHECK: %[[SUB_DATA:.*]] = llvm.getelementptr %[[SUB_DATA_BASE]][{{.*}}] : (!llvm.ptr, i64) -> !llvm.ptr, i32
// CHECK: %[[AFTER_FROM:.*]] = arith.subi %[[SUB_SOURCE_LEN]], {{.*}} : i64
// CHECK: %[[SUB_LEN:.*]] = arith.subi %[[AFTER_FROM]], {{.*}} : i64
// CHECK: %[[SUB_FAT0:.*]] = llvm.mlir.undef : !llvm.struct<(ptr, i64)>
// CHECK: %[[SUB_FAT1:.*]] = llvm.insertvalue %[[SUB_DATA]], %[[SUB_FAT0]][0] : !llvm.struct<(ptr, i64)>
// CHECK: %[[SUB_FAT:.*]] = llvm.insertvalue %[[SUB_LEN]], %[[SUB_FAT1]][1] : !llvm.struct<(ptr, i64)>
// CHECK: %[[EXTRACTED_SUB_LEN:.*]] = llvm.extractvalue %[[SUB_FAT]][1] : !llvm.struct<(ptr, i64)>
// CHECK: call @consume_i64(%[[SLICE_LEN]]) : (i64) -> ()
// CHECK: call @consume_i64(%[[EXTRACTED_SUB_LEN]]) : (i64) -> ()
// CHECK: call @consume_i32(%[[VALUE]]) : (i32) -> ()
// CHECK-NOT: rust.typed.slice_from_array
// CHECK-NOT: rust.typed.ptr_metadata
// CHECK-NOT: rust.typed.subslice
// CHECK-NOT: rust.typed.index_addr
