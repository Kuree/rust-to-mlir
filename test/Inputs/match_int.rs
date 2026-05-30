pub fn classify(x: i32) -> i32 {
    match x {
        0 => 10,
        1 => 20,
        7 => 70,
        _ => -1,
    }
}
