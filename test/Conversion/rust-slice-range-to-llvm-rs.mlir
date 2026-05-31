// RUN: %rust_mir_extract --crate-root %S/../Inputs/slice_fat_pointer.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir=erase-source-mir --convert-rust-typed-to-arith --convert-rust-typed-to-func --convert-rust-typed-to-cf --mem2reg --convert-rust-typed-memory-to-llvm | FileCheck %s --implicit-check-not=unrealized_conversion_cast --implicit-check-not=rust.typed --implicit-check-not=ops::Index::index --implicit-check-not=ops::IndexMut::index_mut

// CHECK-LABEL: func.func @"slice_fat_pointer::run_slice_range_indexing_typed"
// CHECK: cf.assert {{.*}} "slice index starts after end"
// CHECK: cf.assert {{.*}} "range start index out of range for slice"
// CHECK: llvm.getelementptr {{.*}} : (!llvm.ptr, i64) -> !llvm.ptr, i32

// CHECK-LABEL: func.func @"slice_fat_pointer::run_array_range_indexing_typed"
// CHECK: cf.assert {{.*}} "slice index starts after end"
// CHECK: llvm.getelementptr %{{.*}}[0, %{{.*}}] : (!llvm.ptr, i64) -> !llvm.ptr, !llvm.array<6 x i32>

// CHECK-LABEL: func.func @"slice_fat_pointer::run_mut_slice_range_indexing_typed"
// CHECK: cf.assert {{.*}} "slice index starts after end"
// CHECK: llvm.getelementptr %{{.*}}[0, %{{.*}}] : (!llvm.ptr, i64) -> !llvm.ptr, !llvm.array<5 x i32>
// CHECK: llvm.store {{.*}} : i32, !llvm.ptr
