// RUN: rust-opt %s -verify-diagnostics

module {
  rust.mir.func @bad_call {
    rust.mir.block 0 {
      // expected-error@+1 {{expected first body operation to be a MIR operand}}
      rust.mir.call {
        rust.mir.place 0 {
        }
        rust.mir.place 0 {
        }
      }
    }
  }
}
