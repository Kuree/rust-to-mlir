// RUN: rust-opt --convert-rust-typed-to-arith --convert-rust-typed-to-func --convert-rust-typed-to-cf --convert-rust-typed-memory-to-llvm %s | FileCheck %s

!enum = !rust.mir.adt<"set_discriminant_to_llvm::E", [!rust.mir.unit, !rust.typed.tuple<!rust.mir.int<"i32">>]>

module {
  rust.typed.func @set_discriminant_to_llvm {
    %slot = rust.typed.local_slot {index = 0 : i64, name = "_1"} : <!enum>
    rust.typed.block 0 {
      rust.typed.set_discriminant %slot {variant_index = 1 : i64, discriminant = "7"} : !rust.typed.slot<!enum>
      rust.typed.return
    }
  }
}

// CHECK-LABEL: func.func @set_discriminant_to_llvm
// CHECK: %[[TAG:.*]] = llvm.mlir.constant(7 : i64) : i64
// CHECK: %[[TAG_ADDR:.*]] = llvm.getelementptr {{.*}}[0, 0] : (!llvm.ptr) -> !llvm.ptr, !llvm.struct<(i64, struct<()>, struct<(i32)>)>
// CHECK: llvm.store %[[TAG]], %[[TAG_ADDR]] : i64, !llvm.ptr
// CHECK-NOT: rust.typed.set_discriminant
