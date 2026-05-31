// RUN: %rust_mir_extract --crate-root %s --emit-bytecode -o %t.mlirbc
// RUN: not rust-opt %t.mlirbc --rust-lift-typed-mir=erase-source-mir 2>&1 | FileCheck %s

// CHECK: cannot lift drop for type requiring drop glue
// CHECK: rust.mir.drop
// CHECK-SAME: unwind = #rust.unwind<Continue>

pub fn drop_string() -> usize {
    let s = String::from("abc");
    s.len()
}
