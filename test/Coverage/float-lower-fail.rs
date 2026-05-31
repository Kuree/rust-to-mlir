// RUN: %rust_mir_extract --crate-root %s --emit-bytecode -o %t.mlirbc
// RUN: not rust-opt %t.mlirbc --rust-lift-typed-mir=erase-source-mir --sroa --convert-rust-typed-to-arith --convert-rust-typed-to-func --convert-rust-typed-to-cf --mem2reg --convert-rust-typed-memory-to-llvm 2>&1 | FileCheck %s

// CHECK: RigidTy(Float(F32))

pub fn float_add(a: f32, b: f32) -> f32 {
    a + b
}
