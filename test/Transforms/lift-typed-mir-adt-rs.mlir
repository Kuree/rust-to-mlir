// RUN: %rust_mir_extract --crate-root %S/../Inputs/struct_adt.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir=erase-source-mir | FileCheck %s

// CHECK-LABEL: rust.typed.func @"struct_adt::pair_sum_typed"
// CHECK: rust.typed.local_slot {{.*}} : <!rust.mir.adt<"struct_adt::Pair"
// CHECK: rust.typed.aggregate {{.*}} -> !rust.mir.adt<"struct_adt::Pair"
// CHECK: rust.typed.field_addr
// CHECK-SAME: index = 0 : i64
// CHECK: rust.typed.field_addr
// CHECK-SAME: index = 1 : i64
// CHECK: rust.typed.return
