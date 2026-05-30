// RUN: rust-opt --rust-lift-typed-mir %s | FileCheck %s

module {
  rust.mir.func @structured attributes {arg_count = 1 : i64} {
    rust.mir.local {index = 0 : i64, name = "_0", role = "return", rust_type = !rust.mir.int<"i32">}
    rust.mir.local {index = 1 : i64, name = "_1", role = "arg", rust_type = !rust.mir.int<"i32">}
    rust.mir.local {index = 2 : i64, name = "_2", rust_type = !rust.mir.int<"i32">}
    rust.mir.local {index = 3 : i64, name = "_3", rust_type = !rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.int<"i32">>}
    rust.mir.block 0 {
      rust.mir.assign 0 {
        rust.mir.place 2 {
        }
        rust.mir.binary_op attributes {op = "Add"} {
          rust.mir.copy {
            rust.mir.place 1 {
            }
          }
          rust.mir.constant {debug = "3"}
        }
      }
      rust.mir.assign 1 {
        rust.mir.place 0 {
        }
        rust.mir.use {
          rust.mir.copy {
            rust.mir.place 3 {
              rust.mir.projection_field {index = 1 : i64}
            }
          }
        }
      }
      rust.mir.return
    }
  }
}

// CHECK-LABEL: rust.typed.func @structured_typed
// CHECK: rust.typed.const
// CHECK-SAME: debug = "3"
// CHECK: rust.typed.add
// CHECK: rust.typed.field
// CHECK-SAME: index = 1 : i64
// CHECK: rust.typed.store
// CHECK: rust.typed.return
