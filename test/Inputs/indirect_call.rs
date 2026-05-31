unsafe extern "C" {
    fn __rtl_println_i32(value: i32);
}

pub fn inc(x: i32) -> i32 {
    x + 1
}

pub fn double(x: i32) -> i32 {
    x * 2
}

pub fn call_fn_ptr(f: fn(i32) -> i32, x: i32) -> i32 {
    f(x)
}

pub fn choose_and_call(flag: bool, x: i32) -> i32 {
    let f: fn(i32) -> i32 = if flag { inc } else { double };
    f(x)
}

pub fn run_indirect_call() {
    unsafe {
        __rtl_println_i32(call_fn_ptr(inc, 5));
        __rtl_println_i32(choose_and_call(true, 7));
        __rtl_println_i32(choose_and_call(false, 7));
    }
}
