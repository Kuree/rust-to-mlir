// RUN: %rust_mir_extract --crate-root %s --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir=erase-source-mir | FileCheck %s

// CHECK-LABEL: rust.typed.func @"enum_fail::enum_match_typed"
// CHECK: rust.typed.aggregate {{.*}}{discriminant = "0", variant_index = 0 : i64}
// CHECK: rust.typed.aggregate {{.*}}{discriminant = "1", variant_index = 1 : i64}
// CHECK: rust.typed.discriminant
// CHECK: rust.typed.switch_int
// CHECK: rust.typed.unreachable
// CHECK: rust.typed.field_addr {{.*}}variant_index = 1 : i64
// CHECK: rust.typed.field_addr {{.*}}variant_index = 0 : i64

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
