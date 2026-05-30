// RUN: rust-cpu-runner %s -e main | FileCheck %s

module {
  llvm.mlir.global private constant @message("hello shim") : !llvm.array<10 x i8>

  func.func private @__rtl_println_str(!llvm.ptr, i64)

  func.func @main() {
    %message = llvm.mlir.addressof @message : !llvm.ptr
    %len = arith.constant 10 : i64
    func.call @__rtl_println_str(%message, %len) : (!llvm.ptr, i64) -> ()
    return
  }
}

// CHECK: hello shim
