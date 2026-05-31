// RUN: %rust_mir_extract --crate-root %S/../Inputs/loop_forms.rs --emit-bytecode -o %t.loop.mlirbc
// RUN: rust-opt %t.loop.mlirbc --rust-lift-typed-mir=erase-source-mir --convert-rust-typed-to-arith --convert-rust-typed-to-func --convert-rust-typed-to-cf --mem2reg --convert-rust-typed-memory-to-llvm | FileCheck %s --check-prefix=LOOP --implicit-check-not=unrealized_conversion_cast
// RUN: %rust_mir_extract --crate-root %S/../Inputs/for_loop_forms.rs --emit-bytecode -o %t.for.mlirbc
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
