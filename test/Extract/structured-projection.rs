// RUN: %rust_mir_extract --crate-root %s --emit-ndjson | FileCheck %s

pub fn second(pair: (usize, usize)) -> usize {
    pair.1
}

// CHECK: "record":"statement","kind":"Assign"
// CHECK-SAME: "place":{"debug":"_0","local":0,"projection":[]}
// CHECK-SAME: "operand":{"kind":"Copy"
// CHECK-SAME: "local":1,"projection":[{"kind":"Field"
// CHECK-SAME: "index":1
