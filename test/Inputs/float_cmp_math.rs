unsafe extern "C" {
    fn __rtl_println_bool(value: bool);
    fn __rtl_println_f64(value: f64);
    fn __rtl_f64_from_bits(bits: u64) -> f64;
}

pub fn eq_f64(lhs: f64, rhs: f64) -> bool {
    lhs == rhs
}

pub fn ne_f64(lhs: f64, rhs: f64) -> bool {
    lhs != rhs
}

pub fn lt_f64(lhs: f64, rhs: f64) -> bool {
    lhs < rhs
}

pub fn le_f64(lhs: f64, rhs: f64) -> bool {
    lhs <= rhs
}

pub fn gt_f64(lhs: f64, rhs: f64) -> bool {
    lhs > rhs
}

pub fn ge_f64(lhs: f64, rhs: f64) -> bool {
    lhs >= rhs
}

pub fn sqrt_f64(value: f64) -> f64 {
    value.sqrt()
}

pub fn floor_f64(value: f64) -> f64 {
    value.floor()
}

pub fn ceil_f64(value: f64) -> f64 {
    value.ceil()
}

pub fn round_f64(value: f64) -> f64 {
    value.round()
}

pub fn trunc_f64(value: f64) -> f64 {
    value.trunc()
}

pub fn abs_f64(value: f64) -> f64 {
    value.abs()
}

pub fn powi_f64(value: f64, power: i32) -> f64 {
    value.powi(power)
}

pub fn run_float_cmp_math() {
    unsafe {
        let one = __rtl_f64_from_bits(0x3ff0_0000_0000_0000);
        let two = __rtl_f64_from_bits(0x4000_0000_0000_0000);
        let three_point_two_five = __rtl_f64_from_bits(0x400a_0000_0000_0000);
        let three_point_five = __rtl_f64_from_bits(0x400c_0000_0000_0000);
        let three_point_seven_five = __rtl_f64_from_bits(0x400e_0000_0000_0000);
        let four = __rtl_f64_from_bits(0x4010_0000_0000_0000);
        let neg_three_point_seven_five = __rtl_f64_from_bits(0xc00e_0000_0000_0000);
        let neg_five_point_five = __rtl_f64_from_bits(0xc016_0000_0000_0000);
        let nan = __rtl_f64_from_bits(0x7ff8_0000_0000_0000);

        __rtl_println_bool(eq_f64(one, one));
        __rtl_println_bool(ne_f64(one, two));
        __rtl_println_bool(lt_f64(one, two));
        __rtl_println_bool(le_f64(one, one));
        __rtl_println_bool(gt_f64(two, one));
        __rtl_println_bool(ge_f64(two, two));
        __rtl_println_bool(eq_f64(nan, nan));
        __rtl_println_bool(ne_f64(nan, nan));
        __rtl_println_bool(lt_f64(nan, one));

        __rtl_println_f64(sqrt_f64(four));
        __rtl_println_f64(floor_f64(three_point_seven_five));
        __rtl_println_f64(ceil_f64(three_point_two_five));
        __rtl_println_f64(round_f64(three_point_five));
        __rtl_println_f64(trunc_f64(neg_three_point_seven_five));
        __rtl_println_f64(abs_f64(neg_five_point_five));
        __rtl_println_f64(powi_f64(two, 3));
    }
}

pub fn return_f64() -> f64 {
    unsafe { __rtl_f64_from_bits(0x4019_0000_0000_0000) }
}

