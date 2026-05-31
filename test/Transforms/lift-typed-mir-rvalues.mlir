// RUN: rust-opt --rust-lift-typed-mir %s | FileCheck %s

module {
  rust.mir.func @copy_for_deref attributes {arg_count = 1 : i64} {
    rust.mir.local {index = 0 : i64, name = "_0", role = #rust.local_role<return>, rust_type = !rust.mir.int<"i32">}
    rust.mir.local {index = 1 : i64, name = "_1", role = #rust.local_role<arg>, rust_type = !rust.typed.ref<shared, !rust.mir.int<"i32">>}
    rust.mir.local {index = 2 : i64, name = "_2", rust_type = !rust.typed.ref<shared, !rust.mir.int<"i32">>}
    rust.mir.block 0 {
      rust.mir.assign 0 {
        rust.mir.place 2 {
        }
        rust.mir.copy_for_deref {
          rust.mir.place 1 {
          }
        }
      }
      rust.mir.assign 1 {
        rust.mir.place 0 {
        }
        rust.mir.use {
          rust.mir.copy {
            rust.mir.place 2 {
              rust.mir.projection_deref
            }
          }
        }
      }
      rust.mir.return
    }
  }
}

// CHECK-LABEL: rust.typed.func @copy_for_deref_typed
// CHECK: rust.typed.load
// CHECK-SAME: !rust.typed.slot<!rust.typed.ref<shared, !rust.mir.int<"i32">>>
// CHECK: rust.typed.store
// CHECK: rust.typed.load
// CHECK-SAME: !rust.typed.slot<!rust.typed.ref<shared, !rust.mir.int<"i32">>>
// CHECK: rust.typed.load
// CHECK-SAME: !rust.typed.ref<shared, !rust.mir.int<"i32">>
// CHECK: rust.typed.return
