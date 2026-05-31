// RUN: %rust_mir_extract --crate-root %s --emit-bytecode -o %t.mlirbc
// RUN: not rust-opt %t.mlirbc --rust-lift-typed-mir=erase-source-mir 2>&1 | FileCheck %s

// CHECK: expected lifted rust.mir.block to contain a supported terminator
// CHECK: rust.mir.resume

pub fn drop_string() -> usize {
    let s = String::from("abc");
    s.len()
}
