pub fn for_range_i32(n: i32) -> i32 {
    let mut acc = 0;
    for i in 0..n {
        acc += i;
    }
    acc
}

pub fn for_range_u32(n: u32) -> u32 {
    let mut acc = 0u32;
    for i in 1u32..n {
        acc += i;
    }
    acc
}

pub fn for_range_inclusive(n: i32) -> i32 {
    let mut acc = 0;
    for i in 0..=n {
        acc += i;
    }
    acc
}
