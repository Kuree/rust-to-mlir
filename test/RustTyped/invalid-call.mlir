// RUN: rust-opt %s -verify-diagnostics

module {
  rust.typed.func @bad_variadic_call {
    %arg = rust.typed.const {debug = "1", value = 1 : i64} : !rust.mir.int<"i32">
    // expected-error@+1 {{c_variadic calls must use C ABI}}
    rust.typed.call @callee(%arg) {abi = #rust.abi<rust>, c_variadic = true} : (!rust.mir.int<"i32">) -> ()
  }
}
