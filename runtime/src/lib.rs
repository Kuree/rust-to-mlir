use std::alloc::{alloc, alloc_zeroed, dealloc, handle_alloc_error, realloc, Layout};
use std::io::{self, Write};
use std::process;
use std::slice;
use std::str;

unsafe fn bytes_from_raw<'a>(ptr: *const u8, len: usize) -> &'a [u8] {
    if len == 0 {
        return &[];
    }
    assert!(!ptr.is_null(), "runtime shim received null data pointer");
    unsafe { slice::from_raw_parts(ptr, len) }
}

unsafe fn str_from_raw<'a>(ptr: *const u8, len: usize) -> &'a str {
    let bytes = unsafe { bytes_from_raw(ptr, len) };
    unsafe { str::from_utf8_unchecked(bytes) }
}

fn checked_layout(size: usize, align: usize) -> Layout {
    Layout::from_size_align(size, align)
        .unwrap_or_else(|_| panic!("invalid allocation layout: size={}, align={}", size, align))
}

fn zero_sized_ptr(align: usize) -> *mut u8 {
    align.max(1) as *mut u8
}

macro_rules! define_print {
    ($name:ident, $ty:ty) => {
        #[no_mangle]
        pub extern "C" fn $name(value: $ty) {
            print!("{value}");
        }
    };
}

macro_rules! define_println {
    ($name:ident, $ty:ty) => {
        #[no_mangle]
        pub extern "C" fn $name(value: $ty) {
            println!("{value}");
        }
    };
}

define_print!(__rtl_print_i8, i8);
define_print!(__rtl_print_i16, i16);
define_print!(__rtl_print_i32, i32);
define_print!(__rtl_print_i64, i64);
define_print!(__rtl_print_isize, isize);
define_print!(__rtl_print_u8, u8);
define_print!(__rtl_print_u16, u16);
define_print!(__rtl_print_u32, u32);
define_print!(__rtl_print_u64, u64);
define_print!(__rtl_print_usize, usize);
define_print!(__rtl_print_f32, f32);
define_print!(__rtl_print_f64, f64);
define_print!(__rtl_print_bool, bool);

define_println!(__rtl_println_i8, i8);
define_println!(__rtl_println_i16, i16);
define_println!(__rtl_println_i32, i32);
define_println!(__rtl_println_i64, i64);
define_println!(__rtl_println_isize, isize);
define_println!(__rtl_println_u8, u8);
define_println!(__rtl_println_u16, u16);
define_println!(__rtl_println_u32, u32);
define_println!(__rtl_println_u64, u64);
define_println!(__rtl_println_usize, usize);
define_println!(__rtl_println_f32, f32);
define_println!(__rtl_println_f64, f64);
define_println!(__rtl_println_bool, bool);

#[no_mangle]
pub extern "C" fn __rtl_print_char(value: u32) {
    let ch = char::from_u32(value).unwrap_or(char::REPLACEMENT_CHARACTER);
    print!("{ch}");
}

#[no_mangle]
pub extern "C" fn __rtl_println_char(value: u32) {
    let ch = char::from_u32(value).unwrap_or(char::REPLACEMENT_CHARACTER);
    println!("{ch}");
}

#[no_mangle]
pub unsafe extern "C" fn __rtl_print_str(ptr: *const u8, len: usize) {
    let value = unsafe { str_from_raw(ptr, len) };
    print!("{value}");
}

#[no_mangle]
pub unsafe extern "C" fn __rtl_println_str(ptr: *const u8, len: usize) {
    let value = unsafe { str_from_raw(ptr, len) };
    println!("{value}");
}

#[no_mangle]
pub unsafe extern "C" fn __rtl_write_stdout(ptr: *const u8, len: usize) {
    let bytes = unsafe { bytes_from_raw(ptr, len) };
    io::stdout()
        .write_all(bytes)
        .expect("failed writing runtime shim stdout bytes");
}

#[no_mangle]
pub extern "C" fn __rtl_flush_stdout() {
    io::stdout()
        .flush()
        .expect("failed flushing runtime shim stdout");
}

#[no_mangle]
pub extern "C" fn __rtl_print_newline() {
    println!();
}

#[no_mangle]
pub unsafe extern "C" fn __rtl_panic_str(ptr: *const u8, len: usize) -> ! {
    let message = unsafe { str_from_raw(ptr, len) };
    panic!("{}", message);
}

#[no_mangle]
pub extern "C" fn __rtl_abort() -> ! {
    process::abort();
}

#[no_mangle]
pub extern "C" fn __rtl_exit(code: i32) -> ! {
    process::exit(code);
}

#[no_mangle]
pub unsafe extern "C" fn __rtl_alloc(size: usize, align: usize) -> *mut u8 {
    if size == 0 {
        return zero_sized_ptr(align);
    }
    let layout = checked_layout(size, align);
    let ptr = unsafe { alloc(layout) };
    if ptr.is_null() {
        handle_alloc_error(layout);
    }
    ptr
}

#[no_mangle]
pub unsafe extern "C" fn __rtl_alloc_zeroed(size: usize, align: usize) -> *mut u8 {
    if size == 0 {
        return zero_sized_ptr(align);
    }
    let layout = checked_layout(size, align);
    let ptr = unsafe { alloc_zeroed(layout) };
    if ptr.is_null() {
        handle_alloc_error(layout);
    }
    ptr
}

#[no_mangle]
pub unsafe extern "C" fn __rtl_realloc(
    ptr: *mut u8,
    old_size: usize,
    align: usize,
    new_size: usize,
) -> *mut u8 {
    if ptr.is_null() {
        return unsafe { __rtl_alloc(new_size, align) };
    }
    let old_layout = checked_layout(old_size, align);
    if new_size == 0 {
        unsafe { dealloc(ptr, old_layout) };
        return zero_sized_ptr(align);
    }
    let new_ptr = unsafe { realloc(ptr, old_layout, new_size) };
    if new_ptr.is_null() {
        handle_alloc_error(checked_layout(new_size, align));
    }
    new_ptr
}

#[no_mangle]
pub unsafe extern "C" fn __rtl_dealloc(ptr: *mut u8, size: usize, align: usize) {
    if ptr.is_null() || size == 0 {
        return;
    }
    let layout = checked_layout(size, align);
    unsafe { dealloc(ptr, layout) };
}
