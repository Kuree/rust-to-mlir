unsafe extern "C" {
    fn __rtl_println_i32(value: i32);
}

pub fn print_sum() {
    unsafe {
        __rtl_println_i32(1 + 2);
    }
}
