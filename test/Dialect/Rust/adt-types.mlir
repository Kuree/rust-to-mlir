// RUN: rust-opt %s | rust-opt | FileCheck %s
// Round-trips the identified, recursion-capable ADT type through parse/print.

module {
  rust.mir.func @adts {
    rust.mir.block 0 {
      // A plain struct: one variant carrying two scalar fields.
      // CHECK: rust_type = !rust.mir.adt<"demo::Pair", [!rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.int<"i32">>]>
      rust.mir.local {index = 0 : i64, name = "_0", rust_type = !rust.mir.adt<"demo::Pair", [!rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.int<"i32">>]>}

      // A recursive struct: a field references the same identified ADT by name,
      // so the back-edge must print as a bare name reference (no nested body).
      // CHECK: rust_type = !rust.mir.adt<"demo::Node", [!rust.typed.tuple<!rust.mir.int<"i32">, !rust.typed.ref<shared, !rust.mir.adt<"demo::Node">>>]>
      rust.mir.local {index = 1 : i64, name = "_1", rust_type = !rust.mir.adt<"demo::Node", [!rust.typed.tuple<!rust.mir.int<"i32">, !rust.typed.ref<shared, !rust.mir.adt<"demo::Node">>>]>}
    }
  }
}
