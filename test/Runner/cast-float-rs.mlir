// RUN: split-file %s %t
// RUN: %rust_mir_extract --crate-root %t/cast_float.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-cpu-runner %t.mlirbc -e 'cast_float::run_cast_float_typed' | FileCheck %s

// CHECK: 1.5
// CHECK-NEXT: 2.75
// CHECK-NEXT: -7
// CHECK-NEXT: 42
// CHECK-NEXT: -3
// CHECK-NEXT: 4
// CHECK-NEXT: 2147483647
// CHECK-NEXT: 0
// CHECK-NEXT: 0
// CHECK-NEXT: 4294967295

//--- cast_float.rs
unsafe extern "C" {
    fn __rtl_println_f32(value: f32);
    fn __rtl_println_f64(value: f64);
    fn __rtl_println_i32(value: i32);
    fn __rtl_println_u32(value: u32);
    fn __rtl_f32_from_bits(bits: u32) -> f32;
    fn __rtl_f64_from_bits(bits: u64) -> f64;
}

pub fn widen_float(x: f32) -> f64 {
    x as f64
}

pub fn truncate_float(x: f64) -> f32 {
    x as f32
}

pub fn signed_int_to_float(x: i32) -> f32 {
    x as f32
}

pub fn unsigned_int_to_float(x: u32) -> f64 {
    x as f64
}

pub fn float_to_signed_int(x: f32) -> i32 {
    x as i32
}

pub fn float_to_unsigned_int(x: f64) -> u32 {
    x as u32
}

pub fn run_cast_float() {
    unsafe {
        __rtl_println_f64(widen_float(__rtl_f32_from_bits(0x3fc0_0000)));
        __rtl_println_f32(truncate_float(__rtl_f64_from_bits(0x4006_0000_0000_0000)));
        __rtl_println_f32(signed_int_to_float(-7));
        __rtl_println_f64(unsigned_int_to_float(42));
        __rtl_println_i32(float_to_signed_int(__rtl_f32_from_bits(0xc070_0000)));
        __rtl_println_u32(float_to_unsigned_int(__rtl_f64_from_bits(0x4013_0000_0000_0000)));
        __rtl_println_i32(float_to_signed_int(__rtl_f32_from_bits(0x4f00_0000)));
        __rtl_println_i32(float_to_signed_int(__rtl_f32_from_bits(0x7fc0_0000)));
        __rtl_println_u32(float_to_unsigned_int(__rtl_f64_from_bits(0xc000_0000_0000_0000)));
        __rtl_println_u32(float_to_unsigned_int(__rtl_f64_from_bits(0x41f0_0000_0000_0000)));
    }
}
