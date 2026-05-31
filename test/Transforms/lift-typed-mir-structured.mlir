// RUN: rust-opt --rust-lift-typed-mir %s | FileCheck %s

module {
  rust.mir.func @structured attributes {arg_count = 1 : i64} {
    rust.mir.local {index = 0 : i64, name = "_0", role = #rust.local_role<return>, rust_type = !rust.mir.int<"i32">}
    rust.mir.local {index = 1 : i64, name = "_1", role = #rust.local_role<arg>, rust_type = !rust.mir.int<"i32">}
    rust.mir.local {index = 2 : i64, name = "_2", rust_type = !rust.mir.int<"i32">}
    rust.mir.local {index = 3 : i64, name = "_3", rust_type = !rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.int<"i32">>}
    rust.mir.block 0 {
      rust.mir.assign 0 {
        rust.mir.place 2 {
        }
        rust.mir.binary_op attributes {op = #rust.binary_op<Add>} {
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

  rust.mir.func @array_static_index attributes {arg_count = 0 : i64} {
    rust.mir.local {index = 0 : i64, name = "_0", role = #rust.local_role<return>, rust_type = !rust.mir.int<"i32">}
    rust.mir.local {index = 1 : i64, name = "_1", rust_type = !rust.typed.array<!rust.mir.int<"i32">, 2>}
    rust.mir.block 0 {
      rust.mir.assign 0 {
        rust.mir.place 1 {
        }
        rust.mir.aggregate attributes {aggregate_kind = #rust.aggregate_kind<Array>} {
          rust.mir.constant {debug = "1", value = 1 : i64}
          rust.mir.constant {debug = "2", value = 2 : i64}
        }
      }
      rust.mir.assign 1 {
        rust.mir.place 0 {
        }
        rust.mir.use {
          rust.mir.copy {
            rust.mir.place 1 {
              rust.mir.projection_constant_index {from_end = false, min_length = 2 : i64, offset = 1 : i64}
            }
          }
        }
      }
      rust.mir.return
    }
  }

  rust.mir.func @array_static_index_ref attributes {arg_count = 0 : i64} {
    rust.mir.local {index = 0 : i64, name = "_0", role = #rust.local_role<return>, rust_type = !rust.mir.unit}
    rust.mir.local {index = 1 : i64, name = "_1", rust_type = !rust.typed.array<!rust.mir.int<"i32">, 2>}
    rust.mir.local {index = 2 : i64, name = "_2", rust_type = !rust.typed.ref<shared, !rust.mir.int<"i32">>}
    rust.mir.block 0 {
      rust.mir.assign 0 {
        rust.mir.place 1 {
        }
        rust.mir.aggregate attributes {aggregate_kind = #rust.aggregate_kind<Array>} {
          rust.mir.constant {debug = "1", value = 1 : i64}
          rust.mir.constant {debug = "2", value = 2 : i64}
        }
      }
      rust.mir.assign 1 {
        rust.mir.place 2 {
        }
        rust.mir.ref attributes {borrow_kind = #rust.borrow_kind<Shared>, mutability = #rust.mutability<shared>} {
          rust.mir.place 1 {
            rust.mir.projection_constant_index {from_end = false, min_length = 2 : i64, offset = 1 : i64}
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

// CHECK-LABEL: rust.typed.func @array_static_index_typed
// CHECK: rust.typed.aggregate
// CHECK-SAME: -> !rust.typed.array<!rust.mir.int<"i32">, 2>
// CHECK: rust.typed.field
// CHECK-SAME: index = 1 : i64
// CHECK: rust.typed.store
// CHECK: rust.typed.return

// CHECK-LABEL: rust.typed.func @array_static_index_ref_typed
// CHECK: rust.typed.local_slot {{.*}}address_taken = true
// CHECK-SAME: !rust.typed.array<!rust.mir.int<"i32">, 2>
// CHECK: %[[ARRAY_FIELD:.*]] = rust.typed.field_addr
// CHECK-SAME: index = 1 : i64
// CHECK: rust.typed.borrow %[[ARRAY_FIELD]]
// CHECK-SAME: !rust.typed.addr<!rust.mir.int<"i32">>
// CHECK-SAME: -> !rust.typed.ref<shared, !rust.mir.int<"i32">>
// CHECK: rust.typed.return
