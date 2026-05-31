// RUN: %rust_mir_extract --crate-root %s --emit-bytecode -o %t.mlirbc
// RUN: not rust-opt %t.mlirbc --rust-lift-typed-mir=erase-source-mir 2>&1 | FileCheck %s

// CHECK: only range tuple and single-variant ADT aggregate destinations can be lifted

pub fn option_some(x: i32) -> i32 {
    match Some(x) {
        Some(n) => n + 1,
        None => 0,
    }
}
