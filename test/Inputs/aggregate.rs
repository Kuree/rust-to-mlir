pub struct Pair {
    pub a: i32,
    pub b: i32,
}

pub fn make_tuple(a: i32, b: i32) -> (i32, i32) {
    (a, b)
}

pub fn make_pair(a: i32, b: i32) -> Pair {
    Pair { a, b }
}

