unsafe extern "C" {
    fn __rtl_println_i32(value: i32);
    fn __rtl_println_i64(value: i64);
    fn __rtl_println_u64(value: u64);
}

pub fn widen_signed(x: i32) -> i64 {
    x as i64
}

pub fn widen_unsigned(x: u32) -> u64 {
    x as u64
}

pub fn truncate_signed(x: i64) -> i32 {
    x as i32
}

pub fn same_width_signedness(x: u32) -> i32 {
    x as i32
}

pub fn run_cast_int() {
    unsafe {
        __rtl_println_i64(widen_signed(-7));
        __rtl_println_u64(widen_unsigned(7));
        __rtl_println_i32(truncate_signed(4_294_967_298i64));
        __rtl_println_i32(same_width_signedness(4_294_967_294u32));
    }
}
