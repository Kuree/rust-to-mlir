// RUN: split-file %s %t
// RUN: rust-run %t/drop_raii.rs -- -Awarnings | FileCheck %s

// CHECK: 21
// CHECK-NEXT: 22
// CHECK-NEXT: 21

//--- drop_raii.rs
unsafe extern "C" {
    fn __rtl_println_i32(value: i32);
}

struct Trace(i32);

impl Drop for Trace {
    fn drop(&mut self) {
        unsafe {
            __rtl_println_i32(self.0);
        }
    }
}

fn early(flag: bool) {
    let _trace = Trace(21);
    if flag {
        return;
    }

    unsafe {
        __rtl_println_i32(22);
    }
}

fn main() {
    early(true);
    early(false);
}
