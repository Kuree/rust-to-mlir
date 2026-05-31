// RUN: split-file %s %t
// RUN: %rust_mir_extract --crate-root %t/slice_fat_pointer.rs --emit-bytecode -o %t.mlirbc
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

//--- slice_fat_pointer.rs
unsafe extern "C" {
    fn __rtl_println_i32(value: i32);
}

pub fn run_slice_fat_pointer() {
    let values = [10, 20, 30, 40];
    let slice: &[i32] = &values;
    let len = slice.len();
    let third = slice[2];
    let base = if len == 4usize { 100 } else { 0 };

    unsafe {
        __rtl_println_i32(base + third);
    }
}

pub fn run_slice_pattern_subslice() {
    let values = [10, 20, 30, 40];
    let slice: &[i32] = &values;

    if let [_, middle @ .., _] = slice {
        unsafe {
            __rtl_println_i32(middle[0] + middle[1]);
        }
    }
}

pub fn run_slice_range_indexing() {
    let values = [10, 20, 30, 40, 50];
    let slice: &[i32] = &values;
    let start = 1usize;
    let end = 4usize;
    let tail_start = 2usize;
    let head_end = 3usize;
    let inclusive_end = 3usize;
    let to_inclusive_end = 2usize;

    let full = &values[..];
    let middle = &slice[start..end];
    let tail = &slice[tail_start..];
    let head = &slice[..head_end];
    let closed = &slice[start..=inclusive_end];
    let to_closed = &slice[..=to_inclusive_end];

    let full_ok = if full.len() == 5usize { 5 } else { 0 };
    let middle_ok = if middle.len() == 3usize { 3 } else { 0 };
    let tail_ok = if tail.len() == 3usize { 3 } else { 0 };
    let head_ok = if head.len() == 3usize { 3 } else { 0 };
    let closed_ok = if closed.len() == 3usize { 3 } else { 0 };
    let to_closed_ok = if to_closed.len() == 3usize { 3 } else { 0 };

    unsafe {
        __rtl_println_i32(full_ok);
        __rtl_println_i32(middle[0] + middle[2] + middle_ok);
        __rtl_println_i32(tail[0] + tail_ok);
        __rtl_println_i32(head[2] + head_ok);
        __rtl_println_i32(closed[0] + closed[2] + closed_ok);
        __rtl_println_i32(to_closed[2] + to_closed_ok);
    }
}

pub fn run_array_range_indexing() {
    let values = [3, 5, 7, 11, 13, 17];
    let start = 2usize;
    let end = 5usize;
    let tail_start = 3usize;
    let head_end = 4usize;
    let inclusive_end = 3usize;
    let to_inclusive_end = 2usize;

    let middle = &values[start..end];
    let tail = &values[tail_start..];
    let head = &values[..head_end];
    let closed = &values[start..=inclusive_end];
    let to_closed = &values[..=to_inclusive_end];
    let empty_middle = &values[2usize..2usize];
    let empty_tail = &values[6usize..];

    let middle_ok = if middle.len() == 3usize { 3 } else { 0 };
    let tail_ok = if tail.len() == 3usize { 3 } else { 0 };
    let head_ok = if head.len() == 4usize { 4 } else { 0 };
    let closed_ok = if closed.len() == 2usize { 2 } else { 0 };
    let to_closed_ok = if to_closed.len() == 3usize { 3 } else { 0 };
    let empty_ok = if empty_middle.len() == 0usize { 10 } else { 0 }
        + if empty_tail.len() == 0usize { 10 } else { 0 };

    unsafe {
        __rtl_println_i32(middle[0] + middle[2] + middle_ok);
        __rtl_println_i32(tail[0] + tail[2] + tail_ok);
        __rtl_println_i32(head[0] + head[3] + head_ok);
        __rtl_println_i32(closed[0] + closed[1] + closed_ok);
        __rtl_println_i32(to_closed[0] + to_closed[2] + to_closed_ok);
        __rtl_println_i32(empty_ok);
    }
}

pub fn run_mut_slice_range_indexing() {
    let mut values = [1, 2, 3, 4, 5];
    let start = 1usize;
    let end = 4usize;
    let middle = &mut values[start..end];

    middle[0] = 12;
    middle[2] = 24;

    let middle_ok = if middle.len() == 3usize { 3 } else { 0 };

    unsafe {
        __rtl_println_i32(middle[0] + middle[2] + middle_ok);
    }
}
