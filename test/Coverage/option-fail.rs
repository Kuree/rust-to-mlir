// RUN: %rust_mir_extract --crate-root %s --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir=erase-source-mir | FileCheck %s

// CHECK-LABEL: rust.typed.func @"option_fail::option_some_typed"
// CHECK: rust.typed.aggregate {{.*}}{discriminant = "1", variant_index = 1 : i64}
// CHECK: rust.typed.discriminant
// CHECK: rust.typed.switch_int
// CHECK: rust.typed.unreachable
// CHECK: rust.typed.field_addr {{.*}}variant_index = 1 : i64

pub fn option_some(x: i32) -> i32 {
    match Some(x) {
        Some(n) => n + 1,
        None => 0,
    }
}
