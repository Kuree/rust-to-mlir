// RUN: split-file %s %t
// RUN: rust-run %t/drop_enum_structural.rs -- -Awarnings | FileCheck %s

// CHECK: 10
// CHECK-NEXT: 20
// CHECK-NEXT: 30
// CHECK-NEXT: 99

//--- drop_enum_structural.rs
unsafe extern "C" {
    fn __rtl_println_i32(value: i32);
}

struct Leaf(i32);

impl Drop for Leaf {
    fn drop(&mut self) {
        unsafe {
            __rtl_println_i32(self.0);
        }
    }
}

enum E {
    A(Leaf),
    B(Leaf, Leaf),
    C,
}

fn make(flag: i32) -> E {
    if flag == 0 {
        E::A(Leaf(10))
    } else if flag == 1 {
        E::B(Leaf(20), Leaf(30))
    } else {
        E::C
    }
}

fn drop_e(flag: i32) {
    let _e = make(flag);
}

fn main() {
    drop_e(0);
    drop_e(1);
    drop_e(2);
    unsafe {
        __rtl_println_i32(99);
    }
}
