// RUN: split-file %s %t
// RUN: %rust_mir_extract --crate-root %t/print_variants.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-cpu-runner %t.mlirbc -e 'print_variants::print_variants_typed' | FileCheck %s

// CHECK: 12 -34
// CHECK-NEXT: true false
// CHECK-NEXT: prefix suffix
// CHECK-NEXT: A

//--- print_variants.rs
unsafe extern "C" {
    fn __rtl_print_i32(value: i32);
    fn __rtl_println_i64(value: i64);
    fn __rtl_print_bool(value: bool);
    fn __rtl_println_bool(value: bool);
    fn __rtl_print_char(value: u32);
    fn __rtl_println_char(value: u32);
    fn __rtl_print_str(ptr: *const u8, len: usize);
    fn __rtl_println_str(ptr: *const u8, len: usize);
}

pub fn print_variants() {
    let prefix = "prefix ";
    let suffix = "suffix";

    unsafe {
        __rtl_print_i32(12);
        __rtl_print_char(32);
        __rtl_println_i64(-34);

        __rtl_print_bool(true);
        __rtl_print_char(32);
        __rtl_println_bool(false);

        __rtl_print_str(prefix.as_ptr(), prefix.len());
        __rtl_println_str(suffix.as_ptr(), suffix.len());

        __rtl_println_char(65);
    }
}
