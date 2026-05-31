// RUN: split-file %s %t
// RUN: %rust_mir_extract --crate-root %t/print_str_shim.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-cpu-runner %t.mlirbc -e 'print_str_shim::print_str_typed' | FileCheck %s

// CHECK: hello shim

//--- print_str_shim.rs
unsafe extern "C" {
    fn __rtl_println_str(ptr: *const u8, len: usize);
}

pub fn print_str() {
    let message = "hello shim";
    unsafe {
        __rtl_println_str(message.as_ptr(), message.len());
    }
}
