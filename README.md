# RustToLLVM

RustToLLVM is an out-of-tree MLIR project for importing Rust MIR, as exposed by
`rustc_public`, into a project-owned `rust` MLIR dialect.

The stable boundary is this repository's MLIR dialect and NDJSON stream format.
`rustc_public` is still compiler-private. The extractor is pinned by
`rust-toolchain.toml`; update that file and the expected NDJSON tests together
when moving to a newer Rust compiler.

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
  -DLLVM_EXTERNAL_LIT=/home/keyi/workspace/rust-to-llvm/env/bin/lit \
  -DRUST_TO_LLVM_CARGO_EXECUTABLE=/home/keyi/.cargo/bin/cargo \
  -DRUST_TO_LLVM_RUSTC_EXECUTABLE=/home/keyi/.cargo/bin/rustc
ninja -C build
```

Run tests:

```sh
ninja -C build check-rust-to-llvm
```

The configured Cargo and rustc paths are also available in lit tests as
`%cargo` and `%rustc`.

## Translate NDJSON

```sh
build/bin/rust-translate --from-ndjson test/Inputs/add.ndjson
```

The `--rs` and `--cargo` modes invoke `rust-mir-extract`, which requires a
Rust toolchain with `rustc_public` available through `rustc-dev`. The extractor
crate has a local Cargo config that sets `RUSTC_BOOTSTRAP=1`, because
`rustc_public` is still an unstable compiler-private interface.

```sh
build/bin/rust-translate --rs test/Inputs/add.rs \
  --rust-mir-extract build/bin/rust-mir-extract
```

Add MLIR's builtin `--mlir-print-debuginfo` flag to show imported Rust source
locations, including start/end ranges when rustc_public provides them.
