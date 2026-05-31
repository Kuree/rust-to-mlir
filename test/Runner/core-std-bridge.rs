// RUN: rust-run %s -- -Awarnings | FileCheck %s
// RUN: %rust_mir_extract --crate-root %s -S -o %t.mlir --bridge-rust %t.bridge.rs
// RUN: FileCheck %s --check-prefix=MLIR < %t.mlir
// RUN: FileCheck %s --check-prefix=BRIDGE < %t.bridge.rs

extern "C" {
    fn __rtl_println_i32(value: i32);
}

fn max_i32(a: i32, b: i32) -> i32 {
    std::cmp::max(a, b)
}

fn min_i32(a: i32, b: i32) -> i32 {
    core::cmp::min(a, b)
}

fn main() {
    unsafe {
        __rtl_println_i32(max_i32(7, 11));
        __rtl_println_i32(min_i32(7, 11));
    }
}

// CHECK: 11
// CHECK-NEXT: 7

// MLIR: callee_bridge_symbol = "__rust_to_mlir_bridge
// MLIR-SAME: callee_def = "std::cmp::max"
// MLIR: callee_bridge_symbol = "__rust_to_mlir_bridge
// MLIR-SAME: callee_def = "std::cmp::min"

// BRIDGE: pub extern "C" fn __rust_to_mlir_bridge
// BRIDGE: ::std::cmp::max(__arg0, __arg1)
// BRIDGE: pub extern "C" fn __rust_to_mlir_bridge
// BRIDGE: ::std::cmp::min(__arg0, __arg1)
