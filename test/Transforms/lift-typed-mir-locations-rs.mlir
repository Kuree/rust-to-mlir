// RUN: %rust_mir_extract --crate-root %S/../Inputs/bitand_locations.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --rust-lift-typed-mir --mlir-print-debuginfo | FileCheck %s

// CHECK: rust.typed.binop
// CHECK-SAME: loc(#[[ADD_LOC:loc[0-9]+]])
// CHECK: rust.typed.store
// CHECK-SAME: loc(#[[ADD_LOC]])
// CHECK: rust.typed.return
// CHECK-SAME: loc(#[[RETURN_LOC:loc[0-9]+]])
// CHECK-DAG: #[[ADD_LOC]] = loc("{{.*}}bitand_locations.rs":2:5{{.*}})
// CHECK-DAG: #[[RETURN_LOC]] = loc("{{.*}}bitand_locations.rs":3:2{{.*}})
