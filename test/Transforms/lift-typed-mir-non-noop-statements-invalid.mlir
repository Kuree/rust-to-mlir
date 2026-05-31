// RUN: not rust-opt --rust-lift-typed-mir %s 2>&1 | FileCheck %s

module {
  rust.mir.func @set_discriminant_not_noop attributes {arg_count = 0 : i64} {
    rust.mir.local {index = 0 : i64, name = "_0", role = #rust.local_role<return>, rust_type = !rust.mir.unit}
    rust.mir.block 0 {
      rust.mir.set_discriminant {mir_kind = "SetDiscriminant"}
      rust.mir.return
    }
  }
}

// CHECK: cannot lift non-no-op MIR statement yet
