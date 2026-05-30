pub fn inc(x: i32) -> i32 {
    x + 1
}

pub fn caller(x: i32) -> i32 {
    inc(x)
}

