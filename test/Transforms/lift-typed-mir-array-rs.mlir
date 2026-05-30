// RUN: %rust_mir_extract --crate-root %S/../Inputs/array.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir | FileCheck %s

// CHECK-LABEL: rust.typed.func @"array::make_array_typed"
// CHECK: rust.typed.local_slot {{.*}} : <!rust.typed.array<!rust.mir.int<"i32">, 2>>
// CHECK: rust.typed.aggregate
// CHECK-SAME: -> !rust.typed.array<!rust.mir.int<"i32">, 2>
// CHECK: rust.typed.store
// CHECK-SAME: !rust.typed.array<!rust.mir.int<"i32">, 2>
// CHECK: rust.typed.return
