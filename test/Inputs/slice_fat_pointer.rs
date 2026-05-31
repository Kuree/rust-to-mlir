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
