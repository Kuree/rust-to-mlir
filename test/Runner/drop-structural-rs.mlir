// RUN: split-file %s %t
// RUN: rust-run %t/drop_structural.rs -- -Awarnings | FileCheck %s

// CHECK: 4
// CHECK-NEXT: 5
// CHECK-NEXT: 90
// CHECK-NEXT: 3
// CHECK-NEXT: 1
// CHECK-NEXT: 2

//--- drop_structural.rs
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

struct Pair {
    a: Leaf,
    b: Leaf,
}

struct Owner {
    marker: i32,
    x: Leaf,
}

impl Drop for Owner {
    fn drop(&mut self) {
        unsafe {
            __rtl_println_i32(90);
        }
    }
}

fn drop_array() {
    let _arr = [Leaf(4), Leaf(5)];
}

fn main() {
    drop_array();
    let _p = Pair {
        a: Leaf(1),
        b: Leaf(2),
    };
    let _o = Owner {
        marker: 0,
        x: Leaf(3),
    };
}
