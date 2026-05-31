// RUN: %rust_mir_extract --crate-root %S/../Inputs/projected_places.rs --emit-bytecode -o %t.mlirbc
// RUN: rust-cpu-runner %t.mlirbc -e 'projected_places::run_projected_places_typed' | FileCheck %s

// CHECK: 17
// CHECK-NEXT: 50
