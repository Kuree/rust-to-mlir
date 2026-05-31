// RUN: %rust_mir_extract --crate-root %S/../Inputs/slice_fat_pointer.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir=erase-source-mir | FileCheck %s --implicit-check-not=ops::Index::index --implicit-check-not=ops::IndexMut::index_mut

// CHECK-LABEL: rust.typed.func @"slice_fat_pointer::run_slice_range_indexing_typed"
// CHECK: rust.typed.slice_range
// CHECK-SAME: !rust.typed.ref<"shared", !rust.typed.array<!rust.mir.int<"i32">, 5>>
// CHECK: rust.typed.assert {{.*}}msg = "slice index starts after end"
// CHECK: rust.typed.assert {{.*}}msg = "range end index out of range for slice"
// CHECK: rust.typed.slice_range
// CHECK-SAME: !rust.typed.ref<"shared", !rust.typed.slice<!rust.mir.int<"i32">>>
// CHECK: rust.typed.assert {{.*}}msg = "range start index out of range for slice"
// CHECK: rust.typed.slice_range
// CHECK: rust.typed.assert {{.*}}msg = "range end index out of range for slice"
// CHECK: rust.typed.slice_range

// CHECK-LABEL: rust.typed.func @"slice_fat_pointer::run_array_range_indexing_typed"
// CHECK: rust.typed.assert {{.*}}msg = "slice index starts after end"
// CHECK: rust.typed.assert {{.*}}msg = "range end index out of range for slice"
// CHECK: rust.typed.slice_range
// CHECK-SAME: !rust.typed.ref<"shared", !rust.typed.array<!rust.mir.int<"i32">, 6>>
// CHECK: rust.typed.assert {{.*}}msg = "range start index out of range for slice"
// CHECK: rust.typed.slice_range
// CHECK-SAME: !rust.typed.ref<"shared", !rust.typed.array<!rust.mir.int<"i32">, 6>>

// CHECK-LABEL: rust.typed.func @"slice_fat_pointer::run_mut_slice_range_indexing_typed"
// CHECK: rust.typed.assert {{.*}}msg = "slice index starts after end"
// CHECK: rust.typed.assert {{.*}}msg = "range end index out of range for slice"
// CHECK: rust.typed.slice_range
// CHECK-SAME: !rust.typed.ref<"mut", !rust.typed.array<!rust.mir.int<"i32">, 5>>
// CHECK: rust.typed.store {{.*}} : !rust.mir.int<"i32">, !rust.typed.addr<!rust.mir.int<"i32">>
