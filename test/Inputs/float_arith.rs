unsafe extern "C" {
    fn __rtl_println_f32(value: f32);
    fn __rtl_println_f64(value: f64);
    fn __rtl_f32_from_bits(bits: u32) -> f32;
    fn __rtl_f64_from_bits(bits: u64) -> f64;
}

pub fn add_f32(a: f32, b: f32) -> f32 {
    a + b
}

pub fn neg_f32(a: f32) -> f32 {
    -a
}

pub fn rem_f32(a: f32, b: f32) -> f32 {
    a % b
}

pub fn affine_f64(a: f64, b: f64, scale: f64, offset: f64) -> f64 {
    (a + b) * scale - offset
}

pub fn div_f64(a: f64, b: f64) -> f64 {
    a / b
}

pub fn run_float_arith() {
    unsafe {
        __rtl_println_f32(add_f32(
            __rtl_f32_from_bits(0x3fc0_0000),
            __rtl_f32_from_bits(0x4010_0000),
        ));
        __rtl_println_f32(neg_f32(__rtl_f32_from_bits(0x4090_0000)));
        __rtl_println_f32(rem_f32(
            __rtl_f32_from_bits(0x40b0_0000),
            __rtl_f32_from_bits(0x4000_0000),
        ));
        __rtl_println_f64(affine_f64(
            __rtl_f64_from_bits(0x3ff4_0000_0000_0000),
            __rtl_f64_from_bits(0x4006_0000_0000_0000),
            __rtl_f64_from_bits(0x4000_0000_0000_0000),
            __rtl_f64_from_bits(0x3ff0_0000_0000_0000),
        ));
        __rtl_println_f64(div_f64(
            __rtl_f64_from_bits(0x401e_0000_0000_0000),
            __rtl_f64_from_bits(0x4004_0000_0000_0000),
        ));
    }
}
