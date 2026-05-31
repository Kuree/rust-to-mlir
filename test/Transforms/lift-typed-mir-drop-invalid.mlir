// RUN: not rust-opt --rust-lift-typed-mir %s 2>&1 | FileCheck %s

module {
  rust.mir.func @drop_nontrivial attributes {arg_count = 0 : i64} {
    rust.mir.local {index = 0 : i64, name = "_0", role = #rust.local_role<return>, rust_type = !rust.mir.unit}
    rust.mir.local {index = 1 : i64, name = "_1", rust_type = !rust.mir.adt<"drop_nontrivial::NeedsDrop", [!rust.typed.tuple<!rust.mir.int<"i32">>]>}
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

// CHECK: cannot lift drop for type requiring unsupported drop glue
