// RUN: split-file %s %t
// RUN: %rust_mir_extract --crate-root %t/loop_forms.rs --emit-bytecode -o %t.loop.mlirbc
// RUN: rust-opt %t.loop.mlirbc --rust-lift-typed-mir=erase-source-mir --convert-rust-typed-to-arith --convert-rust-typed-to-func --convert-rust-typed-to-cf --mem2reg --convert-rust-typed-memory-to-llvm | FileCheck %s --check-prefix=LOOP --implicit-check-not=unrealized_conversion_cast
// RUN: %rust_mir_extract --crate-root %t/for_loop_forms.rs --emit-bytecode -o %t.for.mlirbc
// RUN: rust-opt %t.for.mlirbc --rust-lift-typed-mir=erase-source-mir --convert-rust-typed-to-arith --convert-rust-typed-to-func --convert-rust-typed-to-cf --mem2reg --convert-rust-typed-memory-to-llvm | FileCheck %s --check-prefix=FOR --implicit-check-not=unrealized_conversion_cast

// LOOP-LABEL: func.func @"loop_forms::while_continue_break_typed"
// LOOP: cf.br
// LOOP: cf.switch

// LOOP-LABEL: func.func @"loop_forms::loop_break_value_typed"
// LOOP: cf.br
// LOOP: return {{.*}} : i32

// LOOP-LABEL: func.func @"loop_forms::labeled_nested_break_typed"
// LOOP: cf.br
// LOOP: return {{.*}} : i32

// LOOP-LABEL: func.func @"loop_forms::while_let_option_typed"
// LOOP: llvm.extractvalue {{.*}}[0]
// LOOP: cf.switch {{.*}} : i64

// FOR-LABEL: func.func @"for_loop_forms::for_range_i32_typed"
// FOR: call @__rust_to_mlir_bridge{{.*}} : (!llvm.ptr, !llvm.ptr) -> ()
// FOR: llvm.extractvalue {{.*}}[0] : !llvm.struct<(i64, struct<()>, struct<(i32)>)>

// FOR-LABEL: func.func @"for_loop_forms::for_range_u32_typed"
// FOR: call @__rust_to_mlir_bridge{{.*}} : (!llvm.ptr, !llvm.ptr) -> ()
// FOR: llvm.extractvalue {{.*}}[0] : !llvm.struct<(i64, struct<()>, struct<(i32)>)>

// FOR-LABEL: func.func @"for_loop_forms::for_range_inclusive_typed"
// FOR: call @__rust_to_mlir_bridge{{.*}} : (!llvm.ptr, !llvm.ptr) -> ()
// FOR: llvm.extractvalue {{.*}}[0] : !llvm.struct<(i64, struct<()>, struct<(i32)>)>

//--- loop_forms.rs
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

//--- for_loop_forms.rs
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
