// RUN: %rust_mir_extract --crate-root %S/../Inputs/drop.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir=erase-source-mir | FileCheck %s

// CHECK-LABEL: rust.typed.func @"drop::drop_local_typed"
// CHECK: rust.typed.local_slot {{.*}}address_taken = true
// CHECK: rust.typed.borrow
// CHECK-SAME: borrow_kind = #rust.borrow_kind<Mut>
// CHECK-SAME: mutability = #rust.mutability<mut>
// CHECK: rust.typed.call @"<drop::NeedsDrop as std::ops::Drop>::drop_typed"
// CHECK-SAME: rust_name = "<drop::NeedsDrop as std::ops::Drop>::drop"
// CHECK: rust.typed.goto
