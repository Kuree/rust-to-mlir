// RUN: %rust_mir_extract --crate-root %S/../Inputs/bitand_locations.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir | FileCheck %s

// CHECK: rust.mir.func @"bitand_locations::mask"
// CHECK: rust.typed.func @"bitand_locations::mask_typed"
// CHECK: rust.typed.local_slot
// CHECK-SAME: index = 0 : i64
// CHECK-SAME: <!rust.mir.int<"i32">>
// CHECK: rust.typed.local_slot
// CHECK-SAME: index = 1 : i64
// CHECK: rust.typed.local_slot
// CHECK-SAME: index = 2 : i64
// CHECK: rust.typed.block 0
// CHECK: rust.typed.load
// CHECK: rust.typed.load
// CHECK: rust.typed.bit_and
// CHECK: rust.typed.store
// CHECK: rust.typed.return
