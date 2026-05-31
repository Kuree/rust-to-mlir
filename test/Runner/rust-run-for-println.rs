// RUN: rust-run %s -- -Awarnings | FileCheck %s

extern "C" {
    fn __rtl_println_i32(value: i32);
}

fn clamp_to_loop_value(value: i32) -> i32 {
    core::cmp::max(value, 2)
}

fn print_i32(value: i32) {
    unsafe {
        __rtl_println_i32(value);
    }
}

fn main() {
    let mut total = 0;

    for value in 0..4 {
        total += clamp_to_loop_value(value);
        print_i32(total);
    }

    for limit in 8..10 {
        print_i32(std::cmp::min(total, limit));
    }
}

// CHECK: 2
// CHECK-NEXT: 4
// CHECK-NEXT: 6
// CHECK-NEXT: 9
// CHECK-NEXT: 8
// CHECK-NEXT: 9
