unsafe extern "C" {
    fn __rtl_println_i32(value: i32);
}

fn choose(x: i32) -> i32 {
    if x > 0 {
        x
    } else {
        -x
    }
}

fn clamp_nonzero(x: i32) -> i32 {
    if x == 0 {
        return 1;
    }

    if x < 0 {
        return -x;
    }

    x
}

fn sum_to(n: i32) -> i32 {
    let mut i = 0;
    let mut acc = 0;
    while i < n {
        acc += i;
        i += 1;
    }
    acc
}

fn classify(x: i32) -> i32 {
    match x {
        0 => 10,
        1 => 20,
        7 => 70,
        _ => -1,
    }
}

fn nested_loop_match(n: i32, mode: i32) -> i32 {
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

fn nested_loops(n: i32, m: i32) -> i32 {
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

pub fn branch_case() -> i32 {
    choose(7) * 10 + choose(-3)
}

pub fn early_return_case() -> i32 {
    clamp_nonzero(0) * 100 + clamp_nonzero(-4) * 10 + clamp_nonzero(7)
}

pub fn loop_case() -> i32 {
    sum_to(6)
}

pub fn match_case() -> i32 {
    classify(0) + classify(1) + classify(7) + classify(3)
}

pub fn nested_case() -> i32 {
    nested_loop_match(6, 1) + nested_loop_match(5, -1) + nested_loops(3, 4)
}

pub fn run_control_flow() {
    unsafe {
        __rtl_println_i32(branch_case());
        __rtl_println_i32(early_return_case());
        __rtl_println_i32(loop_case());
        __rtl_println_i32(match_case());
        __rtl_println_i32(nested_case());
    }
}
