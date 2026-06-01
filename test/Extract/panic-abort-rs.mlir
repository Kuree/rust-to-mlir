// RUN: %rust_mir_extract --crate-root %S/../Inputs/panic_abort.rs -S -- -C panic=abort | FileCheck %s

// CHECK-LABEL: rust.mir.func @"panic_abort::fail"
// CHECK: rust.mir.call{{.*}}callee_name = "std::rt::panic_fmt"
// CHECK-SAME: unwind = #rust.unwind<Unreachable>

// CHECK-LABEL: rust.mir.func @"panic_abort::checked"
// CHECK: rust.mir.assert{{.*}}unwind = #rust.unwind<Unreachable>
