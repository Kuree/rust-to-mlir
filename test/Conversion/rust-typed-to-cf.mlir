// RUN: rust-opt --convert-rust-typed-to-func --convert-rust-typed-to-cf %s | FileCheck %s

module {
  rust.typed.func @branch {
    %cond = rust.typed.const {debug = "true"} : i1
    rust.typed.block 0 {
      rust.typed.switch_int %cond {targets = {false = 2 : i64, true = 1 : i64}} : i1
    }
    rust.typed.block 1 {
      %one = rust.typed.const {debug = "1"} : !rust.mir.int<"i32">
      rust.typed.return %one : !rust.mir.int<"i32">
    }
    rust.typed.block 2 {
      %two = rust.typed.const {debug = "2"} : !rust.mir.int<"i32">
      rust.typed.return %two : !rust.mir.int<"i32">
    }
  }

  rust.typed.func @integer_switch {
    %flag = rust.typed.const {debug = "7"} : i32
    rust.typed.block 0 {
      rust.typed.switch_int %flag {targets = {"0" = 1 : i64, "7" = 2 : i64, default = 3 : i64}} : i32
    }
    rust.typed.block 1 {
      %zero = rust.typed.const {debug = "0"} : !rust.mir.int<"i32">
      rust.typed.return %zero : !rust.mir.int<"i32">
    }
    rust.typed.block 2 {
      %seven = rust.typed.const {debug = "7"} : !rust.mir.int<"i32">
      rust.typed.return %seven : !rust.mir.int<"i32">
    }
    rust.typed.block 3 {
      %fallback = rust.typed.const {debug = "fallback"} : !rust.mir.int<"i32">
      rust.typed.return %fallback : !rust.mir.int<"i32">
    }
  }

  rust.typed.func @assert_branch {
    %cond = rust.typed.const {debug = "true"} : i1
    rust.typed.block 0 {
      rust.typed.assert %cond {msg = "ok"} : i1
      rust.typed.goto {target = 1 : i64}
    }
    rust.typed.block 1 {
      rust.typed.return
    }
  }

  rust.typed.func @unreachable_branch {
    rust.typed.block 0 {
      rust.typed.unreachable
    }
  }
}

// CHECK-LABEL: func.func @branch() -> !rust.mir.int<"i32">
// CHECK: %[[COND:.*]] = rust.typed.const
// CHECK: cf.br ^bb1
// CHECK: ^bb1:
// CHECK: cf.cond_br %[[COND]], ^bb2, ^bb3
// CHECK: ^bb2:
// CHECK: %[[ONE:.*]] = rust.typed.const
// CHECK: return %[[ONE]] : !rust.mir.int<"i32">
// CHECK: ^bb3:
// CHECK: %[[TWO:.*]] = rust.typed.const
// CHECK: return %[[TWO]] : !rust.mir.int<"i32">
// CHECK-NOT: rust.typed.block
// CHECK-NOT: rust.typed.switch_int

// CHECK-LABEL: func.func @integer_switch() -> !rust.mir.int<"i32">
// CHECK: %[[FLAG:.*]] = rust.typed.const
// CHECK: cf.br ^bb1
// CHECK: ^bb1:
// CHECK: cf.switch %[[FLAG]] : i32, [
// CHECK: default: ^bb4
// CHECK: 0: ^bb2
// CHECK: 7: ^bb3
// CHECK: ]
// CHECK-NOT: rust.typed.switch_int

// CHECK-LABEL: func.func @assert_branch()
// CHECK: %[[ASSERT_COND:.*]] = rust.typed.const
// CHECK: cf.assert %[[ASSERT_COND]], "ok"
// CHECK: cf.br
// CHECK-NOT: rust.typed.assert

// CHECK-LABEL: func.func @unreachable_branch()
// CHECK: cf.br ^bb1
// CHECK: ^bb1:
// CHECK: llvm.unreachable
// CHECK-NOT: rust.typed.unreachable
