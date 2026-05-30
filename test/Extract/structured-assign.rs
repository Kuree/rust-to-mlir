// RUN: %rust_mir_extract --crate-root %s --emit-ndjson | FileCheck %s

pub fn mask(a: usize, b: usize) -> usize {
    a & b
}

// CHECK: "record":"statement","kind":"Assign"
// CHECK-SAME: "place":{"debug":"_0","local":0,"projection":[]}
// CHECK-SAME: "rvalue":{"kind":"BinaryOp","op":"BitAnd"
// CHECK-SAME: "lhs":{"kind":"Copy","debug":"Copy(_1)","place":{"debug":"_1","local":1,"projection":[]}}
// CHECK-SAME: "rhs":{"kind":"Copy","debug":"Copy(_2)","place":{"debug":"_2","local":2,"projection":[]}}
