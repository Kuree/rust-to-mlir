pub fn nested_loop_match(n: i32, mode: i32) -> i32 {
    let mut i = 0;
    let mut acc = 0;

    while i < n {
        if (i & 1) == 0 {
            match mode {
                0 => {
                    acc += i;
                }
                1 => {
                    if i < 3 {
                        acc += 10;
                    } else {
                        acc += 20;
                    }
                }
                _ => {
                    acc += 1;
                }
            }
        } else if mode < 0 {
            acc -= i;
        } else {
            acc += 2;
        }

        i += 1;
    }

    acc
}

pub fn nested_loops(n: i32, m: i32) -> i32 {
    let mut i = 0;
    let mut acc = 0;

    while i < n {
        let mut j = 0;
        while j < m {
            if i == j {
                acc += 1;
            } else {
                acc += i + j;
            }
            j += 1;
        }
        i += 1;
    }

    acc
}
