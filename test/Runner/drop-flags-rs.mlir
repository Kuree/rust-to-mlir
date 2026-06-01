// RUN: split-file %s %t
// RUN: rust-run %t/drop_flags.rs -- -Awarnings | FileCheck %s

// CHECK: 10
// CHECK-NEXT: 20
// CHECK-NEXT: 30
// CHECK-NEXT: 40
// CHECK-NEXT: 50
// CHECK-NEXT: 60
// CHECK-NEXT: 70

//--- drop_flags.rs
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

fn maybe_init(flag: bool) {
    let x;
    if flag {
        x = Leaf(10);
    }
}

fn conditional_partial(flag: bool, a: i32, b: i32) {
    let pair = (Leaf(a), Leaf(b));
    if flag {
        let _moved = pair.0;
    }
}

fn overwrite() {
    let mut x = Leaf(60);
    x = Leaf(70);
}

fn main() {
    maybe_init(false);
    maybe_init(true);
    conditional_partial(false, 20, 30);
    conditional_partial(true, 40, 50);
    overwrite();
}
