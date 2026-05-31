// RUN: %rust_mir_extract --crate-root %s --emit-bytecode -o %t.mlirbc
// RUN: not rust-opt %t.mlirbc --rust-lift-typed-mir=erase-source-mir 2>&1 | FileCheck %s

// CHECK: unsupported cast rvalue

pub fn cast_i32_i64(x: i32) -> i64 {
    x as i64
}
