// RUN: split-file %s %t
// RUN: %rust_mir_extract --crate-root %t/struct_adt.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-cpu-runner %t.mlirbc -e 'struct_adt::run_struct_adt_typed' | FileCheck %s

// CHECK: 31

//--- struct_adt.rs
unsafe extern "C" {
    fn __rtl_println_i32(value: i32);
}

pub struct Pair {
    pub a: i32,
    pub b: i32,
}

pub fn pair_sum(x: i32, y: i32) -> i32 {
    let mut pair = Pair { a: x, b: y };
    pair.a += 1;
    pair.a + pair.b
}

pub fn run_struct_adt() {
    unsafe {
        __rtl_println_i32(pair_sum(10, 20));
    }
}
