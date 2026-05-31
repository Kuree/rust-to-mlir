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
