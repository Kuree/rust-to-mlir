unsafe extern "C" {
    fn __rtl_println_i32(value: i32);
}

pub fn run_projected_places() {
    let mut pair = (1, 2);
    pair.0 = 5;
    pair.1 = pair.0 + 7;

    let mut values = [10, 20, 30, 40];
    values[2] = pair.1;

    let idx = 3usize;
    values[idx] = values[2] + 1;

    let replacement = pair.0 + values[0];
    let cell = &mut values[1];
    *cell = replacement;

    unsafe {
        __rtl_println_i32(pair.0 + pair.1);
        __rtl_println_i32(values[0] + values[1] + values[2] + values[3]);
    }
}
