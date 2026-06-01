pub fn noncapturing(x: i32) -> i32 {
    let f = |y: i32| y + 1;
    f(x)
}

pub fn capture_copy(x: i32) -> i32 {
    let add = 3;
    let f = |y: i32| y + add;
    f(x)
}

pub fn capture_move(x: i32) -> i32 {
    let add = 4;
    let f = move |y: i32| y + add;
    f(x)
}

pub fn capture_mut(mut x: i32) -> i32 {
    let mut f = || {
        x = x + 1;
        x
    };
    f()
}
