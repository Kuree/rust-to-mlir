unsafe extern "C" {
    fn __rtl_println_i32(value: i32);
}

pub fn while_continue_break(n: i32) -> i32 {
    let mut i = 0;
    let mut acc = 0;

    while i < n {
        i += 1;
        if i == 2 {
            continue;
        }
        if i == 5 {
            break;
        }
        acc += i;
    }

    acc
}

pub fn loop_break_value(n: i32) -> i32 {
    let mut i = 0;
    let mut acc = 0;

    loop {
        if i == n {
            break acc;
        }
        i += 1;
        if (i & 1) == 0 {
            continue;
        }
        acc += i;
    }
}

pub fn labeled_nested_break(n: i32, m: i32) -> i32 {
    let mut i = 0;
    let mut acc = 0;

    'outer: loop {
        if i >= n {
            break;
        }

        let mut j = 0;
        loop {
            if j >= m {
                break;
            }
            if i == 2 && j == 1 {
                break 'outer;
            }
            if j == 1 {
                j += 1;
                continue;
            }

            acc += i * 10 + j;
            j += 1;
        }

        i += 1;
    }

    acc
}

pub fn while_let_option(n: i32) -> i32 {
    let mut value = Some(0);
    let mut acc = 0;

    while let Some(i) = value {
        if i >= n {
            value = None;
        } else {
            acc += i;
            value = Some(i + 1);
        }
    }

    acc
}

pub fn run_loop_forms() {
    unsafe {
        __rtl_println_i32(while_continue_break(10));
        __rtl_println_i32(loop_break_value(6));
        __rtl_println_i32(labeled_nested_break(5, 3));
        __rtl_println_i32(while_let_option(5));
    }
}
