// RUN: %rust_mir_extract --crate-root %s --emit-ndjson | FileCheck %s

pub fn add_flag(a: usize) -> usize {
    a & 3
}

// CHECK: "record":"statement","kind":"Assign"
// CHECK-SAME: "rvalue":{"kind":"BinaryOp","op":"BitAnd"
// CHECK-SAME: "lhs":{"kind":"Copy","debug":"Copy(_1)","place":{"debug":"_1","local":1,"projection":[]}}
// CHECK-SAME: "rhs":{"kind":"Constant"
// CHECK-SAME: "value":3,"debug":"3"
// CHECK-SAME: "ty":
