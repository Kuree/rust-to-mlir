// RUN: %rust_mir_extract --crate-root %s --emit-bytecode -o %t.mlirbc
// RUN: not rust-opt %t.mlirbc --rust-lift-typed-mir=erase-source-mir 2>&1 | FileCheck %s

// CHECK: only range tuple and single-variant ADT aggregate destinations can be lifted

pub enum Small {
    A(i32),
    B(i32),
}

pub fn enum_match(x: i32) -> i32 {
    let value = if x > 0 { Small::A(x) } else { Small::B(-x) };
    match value {
        Small::A(n) => n + 1,
        Small::B(n) => n + 2,
    }
}
