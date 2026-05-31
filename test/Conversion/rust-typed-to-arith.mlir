// RUN: rust-opt --convert-rust-typed-to-arith %s | FileCheck %s
// RUN: rust-opt --convert-rust-typed-to-arith --mlir-print-debuginfo %s | FileCheck %s --check-prefix=LOC

module attributes {dlti.dl_spec = #dlti.dl_spec<index = 32 : i32, "dlti.endianness" = "little">} {
  rust.typed.func @usize_slots {
    %slot = rust.typed.local_slot {index = 0 : i64, name = "_0"} : <!rust.mir.int<"usize">>
    %lhs = rust.typed.const {debug = "40"} : !rust.mir.int<"usize">
    %rhs = rust.typed.const {debug = "2"} : !rust.mir.int<"usize">
    %sum = rust.typed.add %lhs, %rhs : !rust.mir.int<"usize">, !rust.mir.int<"usize"> -> !rust.mir.int<"usize">
    rust.typed.store %sum, %slot : !rust.mir.int<"usize">, !rust.typed.slot<!rust.mir.int<"usize">>
    %loaded = rust.typed.load %slot : !rust.typed.slot<!rust.mir.int<"usize">> -> !rust.mir.int<"usize">
    rust.typed.return %loaded : !rust.mir.int<"usize">
  }

  rust.typed.func @integer_ops {
    %i0 = rust.typed.const {debug = "-8"} : !rust.mir.int<"i32">
    %i1 = rust.typed.const {debug = "3"} : !rust.mir.int<"i32">
    %u0 = rust.typed.const {debug = "8"} : !rust.mir.int<"u32">
    %u1 = rust.typed.const {debug = "3"} : !rust.mir.int<"u32">
    %add = rust.typed.add %i0, %i1 : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.int<"i32">
    %sub = rust.typed.sub %i0, %i1 : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.int<"i32">
    %mul = rust.typed.mul %i0, %i1 : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.int<"i32">
    %div = rust.typed.div %i0, %i1 : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.int<"i32">
    %rem = rust.typed.rem %i0, %i1 : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.int<"i32">
    %and = rust.typed.bit_and %i0, %i1 : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.int<"i32">
    %or = rust.typed.bit_or %i0, %i1 : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.int<"i32">
    %xor = rust.typed.bit_xor %i0, %i1 : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.int<"i32">
    %shl = rust.typed.shl %i0, %i1 : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.int<"i32">
    %shr = rust.typed.shr %i0, %i1 : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.int<"i32">
    %lt_signed = rust.typed.lt %i0, %i1 : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.bool
    %lt_unsigned = rust.typed.lt %u0, %u1 : !rust.mir.int<"u32">, !rust.mir.int<"u32"> -> !rust.mir.bool
    %eq = rust.typed.eq %i0, %i1 : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.bool
    %ne = rust.typed.ne %i0, %i1 : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.bool
    %le = rust.typed.le %i0, %i1 : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.bool
    %gt = rust.typed.gt %i0, %i1 : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.bool
    %ge = rust.typed.ge %i0, %i1 : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.bool
    %neg = rust.typed.neg %i1 : !rust.mir.int<"i32"> -> !rust.mir.int<"i32">
    %not = rust.typed.not %i1 : !rust.mir.int<"i32"> -> !rust.mir.int<"i32">
    %uninit = rust.typed.const {debug = "uninit"} : !rust.mir.int<"i32">
    rust.typed.return %add, %sub, %mul, %div, %rem, %and, %or, %xor, %shl, %shr, %lt_signed, %lt_unsigned, %eq, %ne, %le, %gt, %ge, %neg, %not, %uninit : !rust.mir.int<"i32">, !rust.mir.int<"i32">, !rust.mir.int<"i32">, !rust.mir.int<"i32">, !rust.mir.int<"i32">, !rust.mir.int<"i32">, !rust.mir.int<"i32">, !rust.mir.int<"i32">, !rust.mir.int<"i32">, !rust.mir.int<"i32">, !rust.mir.bool, !rust.mir.bool, !rust.mir.bool, !rust.mir.bool, !rust.mir.bool, !rust.mir.bool, !rust.mir.bool, !rust.mir.int<"i32">, !rust.mir.int<"i32">, !rust.mir.int<"i32">
  }

  rust.typed.func @checked_ops {
    %i0 = rust.typed.const {debug = "2147483647"} : !rust.mir.int<"i32">
    %i1 = rust.typed.const {debug = "1"} : !rust.mir.int<"i32">
    %u0 = rust.typed.const {debug = "4294967295"} : !rust.mir.int<"u32">
    %u1 = rust.typed.const {debug = "1"} : !rust.mir.int<"u32">
    %add, %add_overflow = rust.typed.checked_add %i0, %i1 : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.int<"i32">, !rust.mir.bool
    %sub, %sub_overflow = rust.typed.checked_sub %i0, %i1 : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.int<"i32">, !rust.mir.bool
    %mul, %mul_overflow = rust.typed.checked_mul %i0, %i1 : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.int<"i32">, !rust.mir.bool
    %uadd, %uadd_overflow = rust.typed.checked_add %u0, %u1 : !rust.mir.int<"u32">, !rust.mir.int<"u32"> -> !rust.mir.int<"u32">, !rust.mir.bool
    %usub, %usub_overflow = rust.typed.checked_sub %u0, %u1 : !rust.mir.int<"u32">, !rust.mir.int<"u32"> -> !rust.mir.int<"u32">, !rust.mir.bool
    %umul, %umul_overflow = rust.typed.checked_mul %u0, %u1 : !rust.mir.int<"u32">, !rust.mir.int<"u32"> -> !rust.mir.int<"u32">, !rust.mir.bool
    rust.typed.return %add, %add_overflow, %sub, %sub_overflow, %mul, %mul_overflow, %uadd, %uadd_overflow, %usub, %usub_overflow, %umul, %umul_overflow : !rust.mir.int<"i32">, !rust.mir.bool, !rust.mir.int<"i32">, !rust.mir.bool, !rust.mir.int<"i32">, !rust.mir.bool, !rust.mir.int<"u32">, !rust.mir.bool, !rust.mir.int<"u32">, !rust.mir.bool, !rust.mir.int<"u32">, !rust.mir.bool
  }

  rust.typed.func @tuple_ops {
    %a = rust.typed.const {debug = "11"} : !rust.mir.int<"i32">
    %b = rust.typed.const {debug = "true"} : !rust.mir.bool
    %tuple = rust.typed.aggregate %a, %b : !rust.mir.int<"i32">, !rust.mir.bool -> !rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.bool>
    %field = rust.typed.field %tuple {index = 1 : i64} : !rust.typed.tuple<!rust.mir.int<"i32">, !rust.mir.bool> -> !rust.mir.bool
    rust.typed.return %field : !rust.mir.bool
  }

  rust.typed.func @locations {
    %lhs = rust.typed.const {debug = "1"} : !rust.mir.int<"i32"> loc("arith.rs":1:1)
    %rhs = rust.typed.const {debug = "2"} : !rust.mir.int<"i32"> loc("arith.rs":1:5)
    %sum = rust.typed.add %lhs, %rhs : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.int<"i32"> loc("arith.rs":1:9)
    rust.typed.return %sum : !rust.mir.int<"i32"> loc("arith.rs":1:13)
  }
}

// CHECK-LABEL: rust.typed.func @usize_slots
// CHECK: %[[SLOT:.*]] = rust.typed.local_slot
// CHECK-SAME: <i32>
// CHECK: %[[LHS:.*]] = arith.constant 40 : i32
// CHECK: %[[RHS:.*]] = arith.constant 2 : i32
// CHECK: %[[SUM:.*]] = arith.addi %[[LHS]], %[[RHS]] : i32
// CHECK: rust.typed.store %[[SUM]], %[[SLOT]] : i32, !rust.typed.slot<i32>
// CHECK: %[[LOADED:.*]] = rust.typed.load %[[SLOT]] : !rust.typed.slot<i32> -> i32
// CHECK: rust.typed.return %[[LOADED]] : i32

// CHECK-LABEL: rust.typed.func @integer_ops
// CHECK: arith.addi
// CHECK: arith.subi
// CHECK: arith.muli
// CHECK: arith.divsi
// CHECK: arith.remsi
// CHECK: arith.andi
// CHECK: arith.ori
// CHECK: arith.xori
// CHECK: arith.shli
// CHECK: arith.shrsi
// CHECK: arith.cmpi slt
// CHECK: arith.cmpi ult
// CHECK: arith.cmpi eq
// CHECK: arith.cmpi ne
// CHECK: arith.cmpi sle
// CHECK: arith.cmpi sgt
// CHECK: arith.cmpi sge
// CHECK: ub.poison : i32
// CHECK: rust.typed.return
// CHECK-SAME: i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i1, i1, i1, i1, i1, i1, i1, i32, i32, i32
// CHECK-NOT: !rust.mir.int<"usize">

// CHECK-LABEL: rust.typed.func @checked_ops
// CHECK: arith.addi {{.*}} : i32
// CHECK: arith.xori {{.*}} : i32
// CHECK: arith.andi {{.*}} : i32
// CHECK: %[[ADD_OVERFLOW:.*]] = arith.cmpi slt, {{.*}} : i32
// CHECK: %[[ADD_TRUE:.*]] = arith.constant true
// CHECK: %[[ADD_OK:.*]] = arith.xori %[[ADD_OVERFLOW]], %[[ADD_TRUE]] : i1
// CHECK: cf.assert %[[ADD_OK]], "attempt to add with overflow"
// CHECK: arith.subi {{.*}} : i32
// CHECK: %[[SUB_OVERFLOW:.*]] = arith.cmpi slt, {{.*}} : i32
// CHECK: %[[SUB_TRUE:.*]] = arith.constant true
// CHECK: %[[SUB_OK:.*]] = arith.xori %[[SUB_OVERFLOW]], %[[SUB_TRUE]] : i1
// CHECK: cf.assert %[[SUB_OK]], "attempt to subtract with overflow"
// CHECK: %[[MUL_LOW:.*]], %[[MUL_HIGH:.*]] = arith.mulsi_extended {{.*}} : i32
// CHECK: %[[MUL_SIGN:.*]] = arith.shrsi %[[MUL_LOW]], {{.*}} : i32
// CHECK: %[[MUL_OVERFLOW:.*]] = arith.cmpi ne, %[[MUL_HIGH]], %[[MUL_SIGN]] : i32
// CHECK: %[[MUL_TRUE:.*]] = arith.constant true
// CHECK: %[[MUL_OK:.*]] = arith.xori %[[MUL_OVERFLOW]], %[[MUL_TRUE]] : i1
// CHECK: cf.assert %[[MUL_OK]], "attempt to multiply with overflow"
// CHECK: %[[UADD_SUM:.*]], %[[UADD_OVERFLOW:.*]] = arith.addui_extended {{.*}} : i32, i1
// CHECK: %[[UADD_TRUE:.*]] = arith.constant true
// CHECK: %[[UADD_OK:.*]] = arith.xori %[[UADD_OVERFLOW]], %[[UADD_TRUE]] : i1
// CHECK: cf.assert %[[UADD_OK]], "attempt to add with overflow"
// CHECK: arith.subi {{.*}} : i32
// CHECK: %[[USUB_OVERFLOW:.*]] = arith.cmpi ult, {{.*}} : i32
// CHECK: %[[USUB_TRUE:.*]] = arith.constant true
// CHECK: %[[USUB_OK:.*]] = arith.xori %[[USUB_OVERFLOW]], %[[USUB_TRUE]] : i1
// CHECK: cf.assert %[[USUB_OK]], "attempt to subtract with overflow"
// CHECK: %[[UMUL_LOW:.*]], %[[UMUL_HIGH:.*]] = arith.mului_extended {{.*}} : i32
// CHECK: %[[UMUL_ZERO:.*]] = arith.constant 0 : i32
// CHECK: %[[UMUL_OVERFLOW:.*]] = arith.cmpi ne, %[[UMUL_HIGH]], %[[UMUL_ZERO]] : i32
// CHECK: %[[UMUL_TRUE:.*]] = arith.constant true
// CHECK: %[[UMUL_OK:.*]] = arith.xori %[[UMUL_OVERFLOW]], %[[UMUL_TRUE]] : i1
// CHECK: cf.assert %[[UMUL_OK]], "attempt to multiply with overflow"
// CHECK: rust.typed.return
// CHECK-SAME: i32, i1, i32, i1, i32, i1, i32, i1, i32, i1, i32, i1
// CHECK-NOT: rust.typed.checked_

// CHECK-LABEL: rust.typed.func @tuple_ops
// CHECK: %[[A:.*]] = arith.constant 11 : i32
// CHECK: %[[B:.*]] = arith.constant true
// CHECK: %[[UNDEF:.*]] = llvm.mlir.undef : !llvm.struct<(i32, i1)>
// CHECK: %[[WITH_A:.*]] = llvm.insertvalue %[[A]], %[[UNDEF]][0] : !llvm.struct<(i32, i1)>
// CHECK: %[[TUPLE:.*]] = llvm.insertvalue %[[B]], %[[WITH_A]][1] : !llvm.struct<(i32, i1)>
// CHECK: %[[FIELD:.*]] = llvm.extractvalue %[[TUPLE]][1] : !llvm.struct<(i32, i1)>
// CHECK: rust.typed.return %[[FIELD]] : i1
// CHECK-NOT: rust.typed.aggregate
// CHECK-NOT: rust.typed.field

// LOC-LABEL: rust.typed.func @locations
// LOC: arith.addi {{.*}} loc(#loc[[ARITH_LOC:[0-9]+]])
// LOC: #loc[[ARITH_LOC]] = loc("arith.rs":1:9)
