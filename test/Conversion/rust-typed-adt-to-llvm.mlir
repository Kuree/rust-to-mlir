// RUN: rust-opt --convert-rust-typed-to-arith %s | FileCheck %s --check-prefix=LLVM
// RUN: rust-opt --sroa %s | FileCheck %s --check-prefix=SROA

// A single-variant nominal struct lowers to an LLVM struct and destructures
// under SROA exactly like the equivalent tuple, via DestructurableTypeInterface.

!pair = !rust.mir.adt<"demo::Pair", [!rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.bool>]>

module {
  rust.typed.func @adt_ops {
    %a = rust.typed.const {debug = "11"} : !rust.mir.int<"i32">
    %b = rust.typed.const {debug = "true"} : !rust.mir.bool
    %adt = rust.typed.aggregate %a, %b : !rust.mir.int<"i32">, !rust.mir.bool -> !pair
    %field = rust.typed.field %adt {index = 0 : i64} : !pair -> !rust.mir.int<"i32">
    rust.typed.return %field : !rust.mir.int<"i32">
  }

  rust.typed.func @adt_slot {
    %slot = rust.typed.local_slot {index = 1 : i64, name = "_1"} : <!pair>
    %a = rust.typed.const {debug = "11"} : !rust.mir.int<"i32">
    %b = rust.typed.const {debug = "true"} : !rust.mir.bool
    %adt = rust.typed.aggregate %a, %b : !rust.mir.int<"i32">, !rust.mir.bool -> !pair
    rust.typed.store %adt, %slot : !pair, !rust.typed.slot<!pair>
    %loaded = rust.typed.load %slot : !rust.typed.slot<!pair> -> !pair
    rust.typed.return %loaded : !pair
  }
}

// The ADT lowers to its variant's LLVM struct; aggregate/field become
// insert/extractvalue with no residual rust.typed aggregate ops.
// LLVM-LABEL: rust.typed.func @adt_ops
// LLVM: %[[UNDEF:.*]] = llvm.mlir.undef : !llvm.struct<(i32, i1)>
// LLVM: llvm.insertvalue
// LLVM: %[[FIELD:.*]] = llvm.extractvalue {{.*}}[0] : !llvm.struct<(i32, i1)>
// LLVM-NOT: rust.typed.aggregate
// LLVM-NOT: rust.typed.field

// SROA splits the single-variant ADT slot into one subslot per field.
// SROA-LABEL: rust.typed.func @adt_slot
// SROA-NOT: !rust.mir.adt
// SROA: rust.typed.local_slot
// SROA-SAME: name = "_1.0"
// SROA-SAME: <!rust.mir.int<"i32">>
// SROA: rust.typed.local_slot
// SROA-SAME: name = "_1.1"
// SROA-SAME: <!rust.mir.bool>
