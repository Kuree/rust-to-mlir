// RUN: %rust_mir_extract --crate-root %s --emit-bytecode -o %t.mlirbc --bridge-rust %t.bridge.rs
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir=erase-source-mir | FileCheck %s
// RUN: FileCheck %s --check-prefix=BRIDGE < %t.bridge.rs

// CHECK: rust.typed.call @__rust_to_mlir_bridgedrop_in_place
// CHECK-SAME: abi = #rust.abi<c>
// CHECK-SAME: rust_name = "core::ptr::drop_in_place"

// BRIDGE: drop_in_place::<::std::string::String>
// BRIDGE: ::core::ptr::drop_in_place::<::std::string::String>

pub fn drop_string() -> usize {
    let s = String::from("abc");
    s.len()
}
