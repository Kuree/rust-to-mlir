// RUN: rust-opt --convert-rust-typed-to-func --convert-rust-typed-to-cf %s -verify-diagnostics

module {
  rust.typed.func @bad_returns {
    // expected-error@+1 {{failed to legalize operation 'rust.typed.block' that was explicitly marked illegal}}
    rust.typed.block 0 {
      %value = rust.typed.const {debug = "1"} : !rust.mir.int<"i32">
      rust.typed.return %value : !rust.mir.int<"i32">
    }
    rust.typed.block 1 {
      %value = rust.typed.const {debug = "2"} : !rust.mir.int<"i64">
      // expected-error@+1 {{expected all rust.typed.return ops to have matching operand types}}
      rust.typed.return %value : !rust.mir.int<"i64">
    }
  }
}
