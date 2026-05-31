// RUN: not rust-run %s -- -Awarnings 2>&1 | FileCheck %s

fn not_main() -> i32 {
    1
}

// CHECK: rust-run: main function not found; expected Rust item `rust_to_llvm_main::main`
