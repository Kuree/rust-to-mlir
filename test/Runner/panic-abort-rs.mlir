// RUN: split-file %s %t
// RUN: not --crash rust-run %t/panic_abort.rs -- -Awarnings -C panic=abort

//--- panic_abort.rs
fn main() {
    panic!("boom")
}
