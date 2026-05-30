// RUN: %rust_mir_extract --crate-root %S/../Inputs/references.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir --convert-rust-typed-to-arith --convert-rust-typed-to-func --convert-rust-typed-to-cf --sroa --mem2reg --convert-rust-typed-memory-to-llvm | FileCheck %s

// CHECK: func.func private @consume_raw(!llvm.ptr)
// CHECK: func.func private @consume_ref(!llvm.ptr)

// CHECK-LABEL: func.func @"references::borrow_i32_typed"
// CHECK: llvm.alloca
// CHECK: llvm.store
// CHECK: call @consume_ref({{.*}}) : (!llvm.ptr) -> ()
// CHECK-NOT: rust.typed.

// CHECK-LABEL: func.func @"references::raw_ptr_i32_typed"
// CHECK: llvm.alloca
// CHECK: llvm.store
// CHECK: call @consume_raw({{.*}}) : (!llvm.ptr) -> ()
// CHECK-NOT: rust.typed.
