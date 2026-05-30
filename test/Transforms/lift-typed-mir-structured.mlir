// RUN: rust-opt --rust-lift-typed-mir %s | FileCheck %s

module {
  rust.mir.func @structured attributes {arg_count = 1 : i64} {
    rust.mir.local {index = 0 : i64, name = "_0", role = "return", rust_type = !rust.mir.int<"i32">}
    rust.mir.local {index = 1 : i64, name = "_1", role = "arg", rust_type = !rust.mir.int<"i32">}
    rust.mir.local {index = 2 : i64, name = "_2", rust_type = !rust.mir.int<"i32">}
    rust.mir.local {index = 3 : i64, name = "_3", rust_type = !rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.int<"i32">>}
    rust.mir.block 0 {
      rust.mir.assign {payload = {place = {local = 2 : i64, projection = []}, rvalue = {kind = "BinaryOp", lhs = {kind = "Copy", place = {local = 1 : i64, projection = []}}, op = "Add", rhs = {debug = "3", kind = "Constant"}}}}
      rust.mir.assign {payload = {place = {local = 0 : i64, projection = []}, rvalue = {kind = "Use", operand = {kind = "Copy", place = {local = 3 : i64, projection = [{index = 1 : i64, kind = "Field"}]}}}}}
      rust.mir.return {payload = {}}
    }
  }
}

// CHECK-LABEL: rust.typed.func @structured_typed
// CHECK: rust.typed.const
// CHECK-SAME: debug = "3"
// CHECK: rust.typed.binop
// CHECK-SAME: op = "Add"
// CHECK: rust.typed.field
// CHECK-SAME: index = 1 : i64
// CHECK: rust.typed.store
// CHECK: rust.typed.return
