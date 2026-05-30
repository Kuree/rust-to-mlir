unsafe extern "C" {
    fn __rtl_println_str(ptr: *const u8, len: usize);
}

pub fn print_str() {
    let message = "hello shim";
    unsafe {
        __rtl_println_str(message.as_ptr(), message.len());
    }
}
