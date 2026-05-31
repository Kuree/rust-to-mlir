// RUN: %rust_mir_extract --crate-root %s --emit-bytecode -o %t.mlirbc
// RUN: not rust-opt %t.mlirbc --rust-lift-typed-mir=erase-source-mir 2>&1 | FileCheck %s

// CHECK: unsupported MIR rvalue op: rust.mir.discriminant

pub fn for_loop(n: i32) -> i32 {
    let mut acc = 0;
    for i in 0..n {
        acc += i;
    }
    acc
}
