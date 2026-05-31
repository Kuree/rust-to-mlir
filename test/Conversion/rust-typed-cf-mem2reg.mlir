// RUN: rust-opt --convert-rust-typed-to-func --convert-rust-typed-to-cf --mem2reg %s | FileCheck %s

module {
  rust.typed.func @branch_mem2reg {
    %slot = rust.typed.local_slot {index = 0 : i64, name = "_0"} : <!rust.mir.int<"i32">>
    %cond = rust.typed.const {debug = "true"} : i1
    rust.typed.block 0 {
      rust.typed.switch_int %cond {targets = {false = 2 : i64, true = 1 : i64}} : i1
    }
    rust.typed.block 1 {
      %one = rust.typed.const {debug = "1"} : !rust.mir.int<"i32">
      rust.typed.store %one, %slot : !rust.mir.int<"i32">, !rust.typed.slot<!rust.mir.int<"i32">>
      rust.typed.goto {target = 3 : i64}
    }
    rust.typed.block 2 {
      %two = rust.typed.const {debug = "2"} : !rust.mir.int<"i32">
      rust.typed.store %two, %slot : !rust.mir.int<"i32">, !rust.typed.slot<!rust.mir.int<"i32">>
      rust.typed.goto {target = 3 : i64}
    }
    rust.typed.block 3 {
      %loaded = rust.typed.load %slot : !rust.typed.slot<!rust.mir.int<"i32">> -> !rust.mir.int<"i32">
      rust.typed.return %loaded : !rust.mir.int<"i32">
    }
  }
}

// CHECK-LABEL: func.func @branch_mem2reg() -> !rust.mir.int<"i32">
// CHECK-NOT: rust.typed.local_slot
// CHECK: cf.cond_br
// CHECK: cf.br ^bb{{[0-9]+}}(%{{.*}} : !rust.mir.int<"i32">)
// CHECK: ^bb{{[0-9]+}}(%[[MERGED:.*]]: !rust.mir.int<"i32">):
// CHECK: return %[[MERGED]] : !rust.mir.int<"i32">
// CHECK-NOT: rust.typed.store
// CHECK-NOT: rust.typed.load
