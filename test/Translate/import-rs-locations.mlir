// RUN: rust-translate --rs %S/../Inputs/add_locations.rs --rust-mir-extract %rust_mir_extract --mlir-print-debuginfo | FileCheck %s

// CHECK: module
// CHECK: rust.mir.func
// CHECK-SAME: rust_name = "add_locations::add"
// CHECK: rust.mir.assign
// CHECK-SAME: loc(#[[ADD_LOC:loc[0-9]+]])
// CHECK: rust.mir.checked_binary_op
// CHECK-SAME: loc(#[[ADD_LOC]])
// CHECK: rust.mir.assert
// CHECK-SAME: loc(#[[ADD_LOC]])
// CHECK: rust.mir.return
// CHECK-SAME: loc(#[[RETURN_LOC:loc[0-9]+]])
// CHECK-DAG: #[[ADD_LOC]] = loc("{{.*}}add_locations.rs":2:5 to :10)
// CHECK-DAG: #[[RETURN_LOC]] = loc("{{.*}}add_locations.rs":3:2)
// CHECK-DAG: loc("{{.*}}add_locations.rs":1:1 to 3:2)
