// RUN: rust-cpu-runner %s -e main | FileCheck %s

module {
  llvm.func @__rtl_println_f32(f32)
  llvm.func @main() {
    %0 = llvm.mlir.constant(1.500000e+00 : f32) : f32
    llvm.call @__rtl_println_f32(%0) : (f32) -> ()
    llvm.return
  }
}

// CHECK: 1.5
