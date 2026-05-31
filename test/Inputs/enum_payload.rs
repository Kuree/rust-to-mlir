unsafe extern "C" {
    fn __rtl_println_i32(value: i32);
}

pub enum Small {
    A(i32),
    B(i32),
}

pub enum Inner {
    Left(i32),
    Right(i32),
}

pub enum Outer {
    Wrap(Inner),
    Pair(i32, i32),
    Empty,
}

#[repr(i32)]
pub enum ReprCode {
    Low = -2,
    High = 5,
}

pub fn option_some(x: i32) -> i32 {
    match Some(x) {
        Some(n) => n + 1,
        None => 0,
    }
}

pub fn option_none() -> i32 {
    let value: Option<i32> = None;
    match value {
        Some(n) => n,
        None => 7,
    }
}

pub fn enum_match(x: i32) -> i32 {
    let value = if x > 0 { Small::A(x) } else { Small::B(-x) };
    match value {
        Small::A(n) => n + 1,
        Small::B(n) => n + 2,
    }
}

pub fn nested_payload(x: i32) -> i32 {
    let value = if x > 0 {
        Outer::Wrap(Inner::Left(x))
    } else {
        Outer::Wrap(Inner::Right(-x))
    };

    match value {
        Outer::Wrap(Inner::Left(n)) => n + 10,
        Outer::Wrap(Inner::Right(n)) => n + 20,
        Outer::Pair(a, b) => a + b,
        Outer::Empty => 0,
    }
}

pub fn pair_payload() -> i32 {
    match Outer::Pair(3, 4) {
        Outer::Pair(a, b) => a * 10 + b,
        _ => 0,
    }
}

pub fn empty_payload() -> i32 {
    match Outer::Empty {
        Outer::Empty => 8,
        _ => 0,
    }
}

pub fn repr_payload(x: i32) -> i32 {
    let value = if x < 0 {
        ReprCode::Low
    } else {
        ReprCode::High
    };

    match value {
        ReprCode::Low => 20,
        ReprCode::High => 30,
    }
}

pub fn run_enum_payload() {
    unsafe {
        __rtl_println_i32(option_some(4));
        __rtl_println_i32(option_none());
        __rtl_println_i32(enum_match(3));
        __rtl_println_i32(enum_match(-3));
        __rtl_println_i32(nested_payload(7));
        __rtl_println_i32(nested_payload(-7));
        __rtl_println_i32(pair_payload());
        __rtl_println_i32(empty_payload());
        __rtl_println_i32(repr_payload(-1));
        __rtl_println_i32(repr_payload(1));
    }
}
