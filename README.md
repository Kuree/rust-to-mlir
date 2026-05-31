# RustToMLIR

RustToMLIR is an out-of-tree MLIR project for importing Rust MIR, as exposed by
`rustc_public`, into a project-owned `rust` MLIR dialect.

The stable boundary is this repository's MLIR dialect and MLIR bytecode/text
emitted by `rust-mir-extract`. `rustc_public` is still compiler-private. The
extractor is pinned by `rust-toolchain.toml`; update that file and the expected
MLIR tests together when moving to a newer Rust compiler.

## Build

This project targets system LLVM/MLIR 20 and a Rust toolchain with compiler
development components installed.

```sh
rustup component add rustc-dev llvm-tools-preview rust-src
```

```sh
cmake -S . -B build -G Ninja \
  -DMLIR_DIR=/usr/lib/llvm-20/lib/cmake/mlir \
  -DLLVM_DIR=/usr/lib/llvm-20/lib/cmake/llvm \
  -DLLVM_EXTERNAL_LIT=$PWD/env/bin/lit \
  -DRUST_TO_MLIR_CARGO_EXECUTABLE=/home/keyi/.cargo/bin/cargo \
  -DRUST_TO_MLIR_RUSTC_EXECUTABLE=/home/keyi/.cargo/bin/rustc
ninja -C build
```

Run tests:

```sh
ninja -C build check-rust-to-mlir
```

The configured Cargo and rustc paths are also available in lit tests as
`%cargo` and `%rustc`.

## Extract Rust MIR

```sh
build/bin/rust-mir-extract --crate-root test/Inputs/arith.rs --emit-bytecode -o /tmp/arith.mlirbc
build/bin/rust-opt /tmp/arith.mlirbc --rust-lift-typed-mir
```

Use `-S` to print textual MLIR directly:

```sh
build/bin/rust-mir-extract --crate-root test/Inputs/arith.rs -S
```

For a Cargo package, use Cargo-driven extraction. The extractor runs as
`RUSTC_WRAPPER`, so crate metadata, target flags, and dependency arguments come
from Cargo instead of being reconstructed by hand:

```sh
build/bin/rust-mir-extract --cargo test/Inputs/cargo-basic -S -o /tmp/cargo.mlir
```

Pass MLIR's builtin `--mlir-print-debuginfo` flag to `rust-opt` to show
imported Rust source locations, including start/end ranges when rustc_public
provides them.

The Rust extractor currently uses `dlopen` to load the project C API library
and is tested on Linux/glibc with LLVM/MLIR 20.

## Execute Rust Source

Use `rust-run` to extract MIR, lower through the RustToMLIR pipeline, and JIT
the source crate's `fn main` without writing MLIR to disk:

```sh
build/bin/rust-run path/to/input.rs
```

The default entry point is the lowered `rust_to_mlir_main::main_typed` symbol.
If `main` returns `i32`, `rust-run` uses that value as its process status
instead of printing it. Pass extra rustc flags after `--`.

The C API also exposes transform and execution hooks for embedding:
`rustMlirLowerRustToLLVM`, `rustMlirRunPassPipeline`, and
`rustMlirExecuteMain`.
