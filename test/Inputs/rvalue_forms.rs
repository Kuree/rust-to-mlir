unsafe extern "C" {
    fn __rtl_println_i32(value: i32);
}

pub fn repeat_array(x: i32) -> i32 {
    let values = [x; 3];
    values[0] + values[2]
}

pub fn repeat_zero_len(x: i32) -> i32 {
    let values = [x; 0];
    values.len() as i32
}

pub fn run_rvalue_forms() {
    unsafe {
        __rtl_println_i32(repeat_array(7));
        __rtl_println_i32(repeat_zero_len(42));
    }
}
