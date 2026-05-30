pub fn sum_to(n: i32) -> i32 {
    let mut i = 0;
    let mut acc = 0;
    while i < n {
        acc += i;
        i += 1;
    }
    acc
}

