pub fn clamp_nonzero(x: i32) -> i32 {
    if x == 0 {
        return 1;
    }

    if x < 0 {
        return -x;
    }

    x
}
