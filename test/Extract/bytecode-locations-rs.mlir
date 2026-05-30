// RUN: %rust_mir_extract --crate-root %S/../Inputs/add_locations.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-opt %t.mlirbc --mlir-print-debuginfo | FileCheck %s

// CHECK: rust.mir.assign
// CHECK-SAME: loc(#[[ADD_LOC:loc[0-9]+]])
// CHECK: rust.mir.return
// CHECK-SAME: loc(#[[RETURN_LOC:loc[0-9]+]])
// CHECK-DAG: #[[ADD_LOC]] = loc("{{.*}}add_locations.rs":2:5{{.*}})
// CHECK-DAG: #[[RETURN_LOC]] = loc("{{.*}}add_locations.rs":3:2{{.*}})
