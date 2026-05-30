// RUN: rust-opt --convert-rust-typed-to-func %s | FileCheck %s

module {
  rust.typed.func @simple attributes {arg_count = 0 : i64, rust_name = "simple", signature = "fn simple() -> i32", span = "simple.rs:1:1: 1:24"} {
    %value = rust.typed.const {debug = "7"} : !rust.mir.int<"i32">
    rust.typed.return %value : !rust.mir.int<"i32">
  }
}

// CHECK-LABEL: func.func @simple() -> !rust.mir.int<"i32">
// CHECK-SAME: attributes
// CHECK-SAME: rust.arg_count = 0 : i64
// CHECK-SAME: rust.rust_name = "simple"
// CHECK-SAME: rust.signature = "fn simple() -> i32"
// CHECK-SAME: rust.span = "simple.rs:1:1: 1:24"
// CHECK: %[[VALUE:.*]] = rust.typed.const
// CHECK-SAME: debug = "7"
// CHECK: return %[[VALUE]] : !rust.mir.int<"i32">
// CHECK-NOT: rust.typed.func
