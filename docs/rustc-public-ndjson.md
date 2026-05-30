# rustc_public NDJSON Bridge

The Rust frontend streams one JSON object per line. This keeps large Rust
crates from requiring a single whole-crate JSON tree in memory.

## Records

Required ordering:

```text
module
function begin
local*
block begin
body-record*
block end
function end
```

The importer maps `record + kind` to a `rust.*` MLIR operation. Unknown records
or unknown MIR variants are hard errors.

Body records are flat on purpose: nested MIR payloads are emitted immediately
after the owning statement, rvalue, terminator, or operand. This keeps the stream
incremental while still preserving one operation per rustc_public MIR enum
variant. Besides `statement`, `rvalue`, `terminator`, `operand`, `place`, and
`projection`, the bridge accepts leaf enum records such as `aggregate_kind`,
`projection_elem`, `binop`, `unop`, `cast_kind`, `borrow_kind`, `raw_ptr_kind`,
`fake_read_cause`, `retag_kind`, `variance`, `non_diverging_intrinsic`,
`runtime_checks`, `unwind_action`, `assert_message`, `mut_borrow_kind`,
`fake_borrow_kind`, `mutability`, `safety`, `pointer_coercion`,
`coroutine_kind`, `coroutine_source`, `coroutine_desugaring`, and
`movability`.

## Example

```json
{"record":"function","event":"begin","name":"add","signature":"fn(i32, i32) -> i32","arg_count":2}
{"record":"local","index":0,"name":"_0","role":"return","ty":"i32"}
{"record":"block","event":"begin","index":0}
{"record":"statement","kind":"Assign","payload":{"debug":"Assign(...)"}}
{"record":"terminator","kind":"Return","payload":{}}
{"record":"block","event":"end","index":0}
{"record":"function","event":"end","name":"add"}
```

## rustc_public Surface

The extractor is based on the current `rustc_public` MIR body API:

- `rustc_public::run!` sets up the compiler context.
- `rustc_public::all_local_items()` enumerates local MIR-bearing items.
- `CrateItem::expect_body()` returns a `Body`.
- `Body` exposes `blocks`, `ret_local()`, `arg_locals()`, `inner_locals()`,
  and `local_decls()`.
- `StatementKind`, `Rvalue`, `TerminatorKind`, `Place`, `Operand`,
  `ProjectionElem`, `AggregateKind`, arithmetic/cast/borrow enums,
  unwind/assert enums, and coroutine payload enums define the MIR variants
  represented by `rust.*` ops.

The local `tools/rust-mir-extract/.cargo/config.toml` sets
`RUSTC_BOOTSTRAP=1` for this crate only. The active Rust toolchain must include
`rustc-dev`, `rust-src`, and `llvm-tools-preview`.

Reference docs:

- https://doc.rust-lang.org/nightly/nightly-rustc/rustc_public/index.html
- https://doc.rust-lang.org/nightly/nightly-rustc/rustc_public/mir/body/index.html
