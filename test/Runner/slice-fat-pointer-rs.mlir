// RUN: %rust_mir_extract --crate-root %S/../Inputs/slice_fat_pointer.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-cpu-runner %t.mlirbc -e 'slice_fat_pointer::run_slice_fat_pointer_typed' | FileCheck %s --check-prefix=FAT
// RUN: rust-cpu-runner %t.mlirbc -e 'slice_fat_pointer::run_slice_pattern_subslice_typed' | FileCheck %s --check-prefix=SUBSLICE
// RUN: rust-cpu-runner %t.mlirbc -e 'slice_fat_pointer::run_slice_range_indexing_typed' | FileCheck %s --check-prefix=RANGE
// RUN: rust-cpu-runner %t.mlirbc -e 'slice_fat_pointer::run_array_range_indexing_typed' | FileCheck %s --check-prefix=ARRAY-RANGE
// RUN: rust-cpu-runner %t.mlirbc -e 'slice_fat_pointer::run_mut_slice_range_indexing_typed' | FileCheck %s --check-prefix=MUT-RANGE

// FAT: 130
// SUBSLICE: 50
// RANGE: 5
// RANGE-NEXT: 63
// RANGE-NEXT: 33
// RANGE-NEXT: 33
// RANGE-NEXT: 63
// RANGE-NEXT: 33
// ARRAY-RANGE: 23
// ARRAY-RANGE-NEXT: 31
// ARRAY-RANGE-NEXT: 18
// ARRAY-RANGE-NEXT: 20
// ARRAY-RANGE-NEXT: 13
// ARRAY-RANGE-NEXT: 20
// MUT-RANGE: 39
