// RUN: rust-opt --convert-rust-typed-to-arith %s | FileCheck %s
// RUN: rust-opt --convert-rust-typed-to-arith --mlir-print-debuginfo %s | FileCheck %s --check-prefix=LOC

module attributes {dlti.dl_spec = #dlti.dl_spec<index = 32 : i32, "dlti.endianness" = "little">} {
  rust.typed.func @usize_slots {
    %slot = rust.typed.local_slot {index = 0 : i64, name = "_0"} : <!rust.mir.int<"usize">>
    %lhs = rust.typed.const {debug = "40"} : !rust.mir.int<"usize">
    %rhs = rust.typed.const {debug = "2"} : !rust.mir.int<"usize">
    %sum = rust.typed.binop %lhs, %rhs {op = "Add"} : !rust.mir.int<"usize">, !rust.mir.int<"usize"> -> !rust.mir.int<"usize">
    rust.typed.store %sum, %slot : !rust.mir.int<"usize">, <!rust.mir.int<"usize">>
    %loaded = rust.typed.load %slot : <!rust.mir.int<"usize">> -> !rust.mir.int<"usize">
    rust.typed.return %loaded : !rust.mir.int<"usize">
  }

  rust.typed.func @integer_ops {
    %i0 = rust.typed.const {debug = "-8"} : !rust.mir.int<"i32">
    %i1 = rust.typed.const {debug = "3"} : !rust.mir.int<"i32">
    %u0 = rust.typed.const {debug = "8"} : !rust.mir.int<"u32">
    %u1 = rust.typed.const {debug = "3"} : !rust.mir.int<"u32">
    %add = rust.typed.binop %i0, %i1 {op = "Add"} : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.int<"i32">
    %sub = rust.typed.binop %i0, %i1 {op = "Sub"} : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.int<"i32">
    %mul = rust.typed.binop %i0, %i1 {op = "Mul"} : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.int<"i32">
    %div = rust.typed.binop %i0, %i1 {op = "Div"} : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.int<"i32">
    %rem = rust.typed.binop %i0, %i1 {op = "Rem"} : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.int<"i32">
    %and = rust.typed.binop %i0, %i1 {op = "BitAnd"} : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.int<"i32">
    %or = rust.typed.binop %i0, %i1 {op = "BitOr"} : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.int<"i32">
    %xor = rust.typed.binop %i0, %i1 {op = "BitXor"} : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.int<"i32">
    %shl = rust.typed.binop %i0, %i1 {op = "Shl"} : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.int<"i32">
    %shr = rust.typed.binop %i0, %i1 {op = "Shr"} : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.int<"i32">
    %lt_signed = rust.typed.binop %i0, %i1 {op = "Lt"} : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.bool
    %lt_unsigned = rust.typed.binop %u0, %u1 {op = "Lt"} : !rust.mir.int<"u32">, !rust.mir.int<"u32"> -> !rust.mir.bool
    %eq = rust.typed.binop %i0, %i1 {op = "Eq"} : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.bool
    %ne = rust.typed.binop %i0, %i1 {op = "Ne"} : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.bool
    %le = rust.typed.binop %i0, %i1 {op = "Le"} : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.bool
    %gt = rust.typed.binop %i0, %i1 {op = "Gt"} : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.bool
    %ge = rust.typed.binop %i0, %i1 {op = "Ge"} : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.bool
    %neg = rust.typed.unop %i1 {op = "Neg"} : !rust.mir.int<"i32"> -> !rust.mir.int<"i32">
    %not = rust.typed.unop %i1 {op = "Not"} : !rust.mir.int<"i32"> -> !rust.mir.int<"i32">
    %uninit = rust.typed.const {debug = "uninit"} : !rust.mir.int<"i32">
    rust.typed.return %add, %sub, %mul, %div, %rem, %and, %or, %xor, %shl, %shr, %lt_signed, %lt_unsigned, %eq, %ne, %le, %gt, %ge, %neg, %not, %uninit : !rust.mir.int<"i32">, !rust.mir.int<"i32">, !rust.mir.int<"i32">, !rust.mir.int<"i32">, !rust.mir.int<"i32">, !rust.mir.int<"i32">, !rust.mir.int<"i32">, !rust.mir.int<"i32">, !rust.mir.int<"i32">, !rust.mir.int<"i32">, !rust.mir.bool, !rust.mir.bool, !rust.mir.bool, !rust.mir.bool, !rust.mir.bool, !rust.mir.bool, !rust.mir.bool, !rust.mir.int<"i32">, !rust.mir.int<"i32">, !rust.mir.int<"i32">
  }

  rust.typed.func @locations {
    %lhs = rust.typed.const {debug = "1"} : !rust.mir.int<"i32"> loc("arith.rs":1:1)
    %rhs = rust.typed.const {debug = "2"} : !rust.mir.int<"i32"> loc("arith.rs":1:5)
    %sum = rust.typed.binop %lhs, %rhs {op = "Add"} : !rust.mir.int<"i32">, !rust.mir.int<"i32"> -> !rust.mir.int<"i32"> loc("arith.rs":1:9)
    rust.typed.return %sum : !rust.mir.int<"i32"> loc("arith.rs":1:13)
  }
}

// CHECK-LABEL: rust.typed.func @usize_slots
// CHECK: %[[SLOT:.*]] = rust.typed.local_slot
// CHECK-SAME: <i32>
// CHECK: %[[LHS:.*]] = arith.constant 40 : i32
// CHECK: %[[RHS:.*]] = arith.constant 2 : i32
// CHECK: %[[SUM:.*]] = arith.addi %[[LHS]], %[[RHS]] : i32
// CHECK: rust.typed.store %[[SUM]], %[[SLOT]] : i32, <i32>
// CHECK: %[[LOADED:.*]] = rust.typed.load %[[SLOT]] : <i32> -> i32
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

// LOC-LABEL: rust.typed.func @locations
// LOC: arith.addi {{.*}} loc(#loc[[ARITH_LOC:[0-9]+]])
// LOC: #loc[[ARITH_LOC]] = loc("arith.rs":1:9)
