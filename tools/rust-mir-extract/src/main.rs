#![feature(rustc_private)]

use std::process;

fn main() {
    process::exit(rust_mir_extract::run_from_env());
}
