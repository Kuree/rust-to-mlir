// RUN: rust-run %s -- -Awarnings > %t.out; test $? -eq 42 && test ! -s %t.out

fn add(a: i32, b: i32) -> i32 {
    a + b
}

fn main() -> i32 {
    add(40, 2)
}
