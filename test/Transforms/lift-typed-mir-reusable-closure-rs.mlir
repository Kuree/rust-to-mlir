// RUN: %rust_mir_extract --crate-root %S/../Inputs/reusable_closure.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir=erase-source-mir | FileCheck %s

// CHECK-LABEL: rust.typed.func @"reusable_closure::reusable_fn_typed"
// CHECK: rust.typed.call {{@_ZN.*apply_twice.*_typed}}
// CHECK-SAME: callee_def = "reusable_closure::apply_twice"

// CHECK-LABEL: rust.typed.func @"reusable_closure::reusable_fnmut_typed"
// CHECK: rust.typed.call {{@_ZN.*apply_twice_mut.*_typed}}
// CHECK-SAME: callee_def = "reusable_closure::apply_twice_mut"

// CHECK-LABEL: rust.typed.func {{@_ZN.*apply_twice_mut.*_typed}}
// CHECK-SAME: rust_name = "reusable_closure::apply_twice_mut::<{closure@
// CHECK: rust.typed.call @"reusable_closure::reusable_fnmut::{closure#0}_typed"
// CHECK-SAME: callee_def = "std::ops::FnMut::call_mut"

// CHECK-LABEL: rust.typed.func {{@_ZN.*apply_twice.*_typed}}
// CHECK-SAME: rust_name = "reusable_closure::apply_twice::<{closure@
// CHECK: rust.typed.call @"reusable_closure::reusable_fn::{closure#0}_typed"
// CHECK-SAME: callee_def = "std::ops::Fn::call"
