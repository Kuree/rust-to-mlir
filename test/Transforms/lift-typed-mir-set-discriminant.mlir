// RUN: rust-opt --rust-lift-typed-mir=erase-source-mir %s | FileCheck %s

module {
  rust.mir.func @set_discriminant_manual attributes {arg_count = 0 : i64} {
    rust.mir.local {index = 0 : i64, name = "_0", role = #rust.local_role<return>, rust_type = !rust.mir.unit}
    rust.mir.local {index = 1 : i64, name = "_1", rust_type = !rust.mir.adt<"set_discriminant_manual::E", [!rust.mir.unit, !rust.typed.tuple<!rust.mir.int<"i32">>]>}
    rust.mir.block 0 {
      rust.mir.set_discriminant attributes {variant_index = 1 : i64, discriminant = "7", mir_kind = "SetDiscriminant"} {
        rust.mir.place 1 {
        }
      }
      rust.mir.return
    }
  }
}

// CHECK-LABEL: rust.typed.func @set_discriminant_manual_typed
// CHECK: rust.typed.set_discriminant
// CHECK-SAME: discriminant = "7"
// CHECK-SAME: variant_index = 1 : i64
// CHECK: rust.typed.return
