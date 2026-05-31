// RUN: rust-opt %s -verify-diagnostics --split-input-file

module {
  rust.typed.func @bad_load {
    %slot = rust.typed.local_slot {index = 0 : i64, name = "_0"} : <!rust.mir.int<"i32">>
    // expected-error@+1 {{result type must match address element type}}
    %bad = rust.typed.load %slot : !rust.typed.slot<!rust.mir.int<"i32">> -> !rust.mir.int<"i64">
  }
}

// -----

module {
  rust.typed.func @bad_store {
    %slot = rust.typed.local_slot {index = 1 : i64, name = "_1"} : <!rust.mir.int<"i32">>
    %value = rust.typed.const {debug = "wide"} : !rust.mir.int<"i64">
    // expected-error@+1 {{value type must match address element type}}
    rust.typed.store %value, %slot : !rust.mir.int<"i64">, !rust.typed.slot<!rust.mir.int<"i32">>
  }
}
