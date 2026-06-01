// RUN: %rust_mir_extract --crate-root %S/../Inputs/panic_abort.rs --emit-bytecode -o %t.mlirbc -- -C panic=abort
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir=erase-source-mir | FileCheck %s

// CHECK-LABEL: rust.typed.func @"panic_abort::fail_typed"
// CHECK: rust.typed.call @__rtl_abort()
// CHECK-SAME: rust_name = "__rtl_abort"
// CHECK-SAME: unwind = #rust.unwind<Unreachable>
// CHECK: rust.typed.unreachable

// CHECK-LABEL: rust.typed.func @"panic_abort::checked_typed"
// CHECK: rust.typed.assert{{.*}}unwind = #rust.unwind<Unreachable>
