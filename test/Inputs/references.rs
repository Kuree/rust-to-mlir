unsafe extern "C" {
    fn consume_ref(value: &i32);
    fn consume_raw(value: *const i32);
}

pub fn borrow_i32() {
    let value = 3i32;
    unsafe {
        consume_ref(&value);
    }
}

pub fn raw_ptr_i32() {
    let value = 3i32;
    let ptr = &raw const value;
    unsafe {
        consume_raw(ptr);
    }
}

pub fn borrow_tuple_field() {
    let pair = (1i32, 2i32);
    unsafe {
        consume_ref(&pair.1);
    }
}

pub fn raw_ptr_tuple_field() {
    let pair = (1i32, 2i32);
    let ptr = &raw const pair.1;
    unsafe {
        consume_raw(ptr);
    }
}
