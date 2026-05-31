// RUN: %rust_mir_extract --crate-root %s --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir=erase-source-mir | FileCheck %s
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir=erase-source-mir --convert-rust-typed-to-arith --convert-rust-typed-to-func --convert-rust-typed-to-cf --mem2reg --convert-rust-typed-memory-to-llvm | FileCheck %s --check-prefix=LOWER

// CHECK-LABEL: rust.typed.func @"for_loop_fail::for_loop_typed"
// CHECK: rust.typed.discriminant
// CHECK: rust.typed.switch_int
// CHECK: rust.typed.field_addr {{.*}}variant_index = 1 : i64

// LOWER-LABEL: func.func @"for_loop_fail::for_loop_typed"
// LOWER: call @__rust_to_mlir_bridge{{.*}} : (!llvm.ptr, !llvm.ptr) -> ()
// LOWER: llvm.extractvalue {{.*}}[0] : !llvm.struct<(i64, struct<()>, struct<(i32)>)>

pub fn for_loop(n: i32) -> i32 {
    let mut acc = 0;
    for i in 0..n {
        acc += i;
    }
    acc
}
