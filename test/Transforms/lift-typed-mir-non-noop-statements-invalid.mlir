// RUN: not rust-opt --rust-lift-typed-mir %s 2>&1 | FileCheck %s

module {
  rust.mir.func @intrinsic_not_noop attributes {arg_count = 0 : i64} {
    rust.mir.local {index = 0 : i64, name = "_0", role = #rust.local_role<return>, rust_type = !rust.mir.unit}
    rust.mir.block 0 {
      rust.mir.intrinsic {mir_kind = "Intrinsic"}
      rust.mir.return
    }
  }
}

// CHECK: cannot lift non-no-op MIR statement yet
