unsafe extern "C" {
    fn __rust_to_llvm_print_i32(value: i32);
}

pub fn print_sum() {
    unsafe {
        __rust_to_llvm_print_i32(1 + 2);
    }
}
