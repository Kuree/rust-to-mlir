#![allow(dead_code)]

unsafe extern "C" {
    fn __rtl_println_i32(value: i32);
}

pub fn apply_twice<F: Fn(i32) -> i32>(f: F, x: i32) -> i32 {
    f(x) + f(x + 1)
}

pub fn apply_twice_mut<F: FnMut() -> i32>(mut f: F) -> i32 {
    f() + f()
}

pub fn reusable_fn(x: i32) -> i32 {
    let add = 2;
    let f = |y: i32| y + add;
    apply_twice(f, x)
}

pub fn reusable_fnmut(mut x: i32) -> i32 {
    let bump = || {
        x = x + 1;
        x
    };
    apply_twice_mut(bump)
}

fn main() {
    unsafe {
        __rtl_println_i32(reusable_fn(10));
        __rtl_println_i32(reusable_fnmut(10));
    }
}
