// RUN: rust-opt --rust-lift-typed-mir %s | FileCheck %s

module {
  rust.mir.func @noop_statements attributes {arg_count = 1 : i64} {
    rust.mir.local {index = 0 : i64, name = "_0", role = #rust.local_role<return>, rust_type = !rust.mir.int<"i32">}
    rust.mir.local {index = 1 : i64, name = "_1", role = #rust.local_role<arg>, rust_type = !rust.mir.int<"i32">}
    rust.mir.block 0 {
      rust.mir.storage_live {mir_kind = "StorageLive"}
      rust.mir.fake_read {mir_kind = "FakeRead"}
      rust.mir.deinit {mir_kind = "Deinit"}
      rust.mir.retag {mir_kind = "Retag"}
      rust.mir.place_mention {mir_kind = "PlaceMention"}
      rust.mir.ascribe_user_type {mir_kind = "AscribeUserType"}
      rust.mir.coverage {mir_kind = "Coverage"}
      rust.mir.const_eval_counter {mir_kind = "ConstEvalCounter"}
      rust.mir.nop {mir_kind = "Nop"}
      rust.mir.assign 0 {
        rust.mir.place 0 {
        }
        rust.mir.use {
          rust.mir.copy {
            rust.mir.place 1 {
            }
          }
        }
      }
      rust.mir.storage_dead {mir_kind = "StorageDead"}
      rust.mir.return
    }
  }
}

// CHECK-LABEL: rust.typed.func @noop_statements_typed
// CHECK: rust.typed.load
// CHECK-NOT: storage_live
// CHECK-NOT: fake_read
// CHECK-NOT: deinit
// CHECK-NOT: retag
// CHECK-NOT: place_mention
// CHECK-NOT: ascribe_user_type
// CHECK-NOT: coverage
// CHECK-NOT: const_eval_counter
// CHECK-NOT: storage_dead
// CHECK: rust.typed.store
// CHECK: rust.typed.return
