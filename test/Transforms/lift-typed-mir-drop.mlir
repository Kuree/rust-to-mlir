// RUN: rust-opt --rust-lift-typed-mir %s | FileCheck %s

module {
  rust.mir.func @drop_trivial attributes {arg_count = 0 : i64} {
    rust.mir.local {index = 0 : i64, name = "_0", role = #rust.local_role<return>, rust_type = !rust.mir.unit}
    rust.mir.local {index = 1 : i64, name = "_1", rust_type = !rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.bool>}
    rust.mir.block 0 {
      rust.mir.drop attributes {target = 1 : i64} {
        rust.mir.place 1 {
        }
      }
    }
    rust.mir.block 1 {
      rust.mir.return
    }
  }
}

// CHECK-LABEL: rust.typed.func @drop_trivial_typed
// CHECK: rust.typed.block 0
// CHECK: rust.typed.goto
// CHECK-SAME: target = 1 : i64
// CHECK: rust.typed.block 1
// CHECK: rust.typed.return
