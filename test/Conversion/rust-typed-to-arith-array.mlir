// RUN: rust-opt --convert-rust-typed-to-arith %s | FileCheck %s

module {
  rust.typed.func @array_lower {
    rust.typed.block 0 {
      %a = rust.typed.const {debug = "11"} : !rust.mir.int<"i32">
      %b = rust.typed.const {debug = "22"} : !rust.mir.int<"i32">
      %array = rust.typed.aggregate %a, %b : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.typed.array<!rust.mir.int<"i32">, 2>
      %field = rust.typed.field %array {index = 1 : i64} : !rust.typed.array<!rust.mir.int<"i32">, 2> -> !rust.mir.int<"i32">
      rust.typed.return %field : !rust.mir.int<"i32">
    }
  }
}

// CHECK-LABEL: rust.typed.func @array_lower
// CHECK: %[[A:.*]] = arith.constant 11 : i32
// CHECK: %[[B:.*]] = arith.constant 22 : i32
// CHECK: %[[UNDEF:.*]] = llvm.mlir.undef : !llvm.array<2 x i32>
// CHECK: %[[WITH_A:.*]] = llvm.insertvalue %[[A]], %[[UNDEF]][0] : !llvm.array<2 x i32>
// CHECK: %[[ARRAY:.*]] = llvm.insertvalue %[[B]], %[[WITH_A]][1] : !llvm.array<2 x i32>
// CHECK: %[[FIELD:.*]] = llvm.extractvalue %[[ARRAY]][1] : !llvm.array<2 x i32>
// CHECK: rust.typed.return %[[FIELD]] : i32
