//===- main.rs - Rust MIR extraction driver --------------------*- Rust -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#![feature(rustc_private)]

use std::process;

fn main() {
    process::exit(rust_mir_extract::run_from_env());
}
