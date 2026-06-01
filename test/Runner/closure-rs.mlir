// RUN: split-file %s %t
// RUN: rust-run %t/closure.rs -- -Awarnings | FileCheck %s

// CHECK: 11
// CHECK-NEXT: 13
// CHECK-NEXT: 14
// CHECK-NEXT: 11

//--- closure.rs
unsafe extern "C" {
    fn __rtl_println_i32(value: i32);
}

pub fn noncapturing(x: i32) -> i32 {
    let f = |y: i32| y + 1;
    f(x)
}

pub fn capture_copy(x: i32) -> i32 {
    let add = 3;
    let f = |y: i32| y + add;
    f(x)
}

pub fn capture_move(x: i32) -> i32 {
    let add = 4;
    let f = move |y: i32| y + add;
    f(x)
}

pub fn capture_mut(mut x: i32) -> i32 {
    let mut f = || {
        x = x + 1;
        x
    };
    f()
}

fn main() {
    unsafe {
        __rtl_println_i32(noncapturing(10));
        __rtl_println_i32(capture_copy(10));
        __rtl_println_i32(capture_move(10));
        __rtl_println_i32(capture_mut(10));
    }
}
