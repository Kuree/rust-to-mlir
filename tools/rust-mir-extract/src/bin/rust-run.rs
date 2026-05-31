//===- rust-run.rs - RustToMLIR runner driver ------------------*- Rust -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#![feature(rustc_private)]

use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::{self, Command};

const DRIVER_CRATE_NAME: &str = "rust_to_mlir_main";

struct Options {
    input: PathBuf,
    entry: String,
    runtime_library: Option<PathBuf>,
    rust_target_libdir: Option<PathBuf>,
    shared_libs: Vec<PathBuf>,
    rustc_passthrough: Vec<String>,
}

fn usage() -> &'static str {
    "usage: rust-run [options] <input.rs> [-- <rustc args>]\n\
     options:\n\
       -e, --entry SYMBOL              lowered entry symbol to execute\n\
       --runtime-library PATH          RustToMLIR runtime shim shared library\n\
       --rust-target-libdir PATH       Rust target libdir containing libstd\n\
       --shared-lib PATH               additional shared library for the JIT"
}

fn parse_options(args: impl IntoIterator<Item = String>) -> Result<Options, String> {
    let mut input = None;
    let mut entry = None;
    let mut runtime_library = None;
    let mut rust_target_libdir = None;
    let mut shared_libs = Vec::new();
    let mut rustc_passthrough = Vec::new();

    let mut iter = args.into_iter().peekable();
    while let Some(arg) = iter.next() {
        if arg == "--" {
            rustc_passthrough.extend(iter);
            break;
        }

        match arg.as_str() {
            "-h" | "--help" => return Err(usage().to_string()),
            "-e" | "--entry" => {
                entry = Some(
                    iter.next()
                        .ok_or_else(|| "--entry requires a symbol".to_string())?,
                );
            }
            "--runtime-library" => {
                runtime_library =
                    Some(PathBuf::from(iter.next().ok_or_else(|| {
                        "--runtime-library requires a path".to_string()
                    })?));
            }
            "--rust-target-libdir" => {
                rust_target_libdir =
                    Some(PathBuf::from(iter.next().ok_or_else(|| {
                        "--rust-target-libdir requires a path".to_string()
                    })?));
            }
            "--shared-lib" => {
                shared_libs.push(PathBuf::from(
                    iter.next()
                        .ok_or_else(|| "--shared-lib requires a path".to_string())?,
                ));
            }
            _ if arg.starts_with('-') => return Err(format!("unknown argument: {arg}")),
            _ => {
                if input.replace(PathBuf::from(arg)).is_some() {
                    return Err("only one input Rust file is supported".to_string());
                }
            }
        }
    }

    if rustc_passthrough
        .iter()
        .any(|arg| arg == "--crate-name" || arg.starts_with("--crate-name="))
    {
        return Err("rust-run manages --crate-name so it can find main".to_string());
    }

    let input = input.ok_or_else(|| usage().to_string())?;
    let entry = entry.unwrap_or_else(|| format!("{DRIVER_CRATE_NAME}::main_typed"));
    Ok(Options {
        input,
        entry,
        runtime_library,
        rust_target_libdir,
        shared_libs,
        rustc_passthrough,
    })
}

fn current_exe_dir() -> Option<PathBuf> {
    env::current_exe()
        .ok()
        .and_then(|path| path.parent().map(Path::to_path_buf))
}

fn runtime_library_filename() -> &'static str {
    #[cfg(target_os = "windows")]
    {
        "rust_to_mlir_runtime.dll"
    }
    #[cfg(target_os = "macos")]
    {
        "librust_to_mlir_runtime.dylib"
    }
    #[cfg(all(not(target_os = "windows"), not(target_os = "macos")))]
    {
        "librust_to_mlir_runtime.so"
    }
}

fn is_rust_std_shared_library(filename: &str) -> bool {
    #[cfg(target_os = "windows")]
    {
        filename.starts_with("std-") && filename.ends_with(".dll")
    }
    #[cfg(target_os = "macos")]
    {
        filename.starts_with("libstd-") && filename.ends_with(".dylib")
    }
    #[cfg(all(not(target_os = "windows"), not(target_os = "macos")))]
    {
        filename.starts_with("libstd-") && filename.ends_with(".so")
    }
}

fn find_runtime_library(explicit: Option<&Path>) -> Result<PathBuf, String> {
    let mut candidates = Vec::new();
    if let Some(path) = explicit {
        candidates.push(path.to_path_buf());
    }
    if let Ok(path) = env::var("RUST_TO_MLIR_RUNTIME_LIBRARY") {
        candidates.push(PathBuf::from(path));
    }
    if let Some(path) = option_env!("RUST_TO_MLIR_RUNTIME_LIBRARY") {
        candidates.push(PathBuf::from(path));
    }
    if let Some(exe_dir) = current_exe_dir() {
        candidates.push(
            exe_dir
                .join("..")
                .join("lib")
                .join(runtime_library_filename()),
        );
        candidates.push(exe_dir.join(runtime_library_filename()));
    }

    for candidate in &candidates {
        if candidate.is_file() {
            return Ok(candidate.clone());
        }
    }

    Err(format!(
        "RustToMLIR runtime shim was not found\nsearched:\n  {}",
        candidates
            .iter()
            .map(|path| path.display().to_string())
            .collect::<Vec<_>>()
            .join("\n  ")
    ))
}

fn rustc_program() -> String {
    env::var("RUSTC")
        .ok()
        .or_else(|| option_env!("RUST_TO_MLIR_RUSTC_EXECUTABLE").map(str::to_string))
        .unwrap_or_else(|| "rustc".to_string())
}

fn query_rust_target_libdir() -> Result<PathBuf, String> {
    let rustc = rustc_program();
    let output = Command::new(&rustc)
        .args(["--print", "target-libdir"])
        .output()
        .map_err(|err| format!("failed to run '{rustc} --print target-libdir': {err}"))?;
    if !output.status.success() {
        return Err(format!(
            "'{rustc} --print target-libdir' failed with status {}",
            output.status
        ));
    }
    let stdout = String::from_utf8(output.stdout)
        .map_err(|err| format!("rustc target-libdir output was not UTF-8: {err}"))?;
    let path = stdout.trim();
    if path.is_empty() {
        return Err("rustc returned an empty target-libdir".to_string());
    }
    Ok(PathBuf::from(path))
}

fn find_rust_std_shared_library(explicit_libdir: Option<&Path>) -> Result<PathBuf, String> {
    let libdir = explicit_libdir
        .map(Path::to_path_buf)
        .map(Ok)
        .unwrap_or_else(query_rust_target_libdir)?;
    let mut matches = fs::read_dir(&libdir)
        .map_err(|err| format!("failed to scan {}: {err}", libdir.display()))?
        .filter_map(|entry| entry.ok().map(|entry| entry.path()))
        .filter(|path| {
            path.file_name()
                .and_then(|name| name.to_str())
                .is_some_and(is_rust_std_shared_library)
        })
        .collect::<Vec<_>>();
    matches.sort();
    matches.into_iter().next().ok_or_else(|| {
        format!(
            "could not find Rust std shared library in {}",
            libdir.display()
        )
    })
}

fn unique_push(paths: &mut Vec<PathBuf>, path: PathBuf) {
    if !paths.iter().any(|existing| existing == &path) {
        paths.push(path);
    }
}

fn resolve_shared_libs(opts: &Options) -> Result<Vec<String>, String> {
    let mut libs = Vec::new();
    unique_push(
        &mut libs,
        find_rust_std_shared_library(opts.rust_target_libdir.as_deref())?,
    );
    unique_push(
        &mut libs,
        find_runtime_library(opts.runtime_library.as_deref())?,
    );
    for lib in &opts.shared_libs {
        unique_push(&mut libs, lib.clone());
    }
    Ok(libs
        .into_iter()
        .map(|path| path.to_string_lossy().into_owned())
        .collect())
}

fn run() -> Result<i32, String> {
    let opts = parse_options(env::args().skip(1))?;
    if !opts.input.is_file() {
        return Err(format!(
            "input Rust file not found: {}",
            opts.input.display()
        ));
    }

    let shared_libs = resolve_shared_libs(&opts)?;
    rust_mir_extract::execute_crate_root_main(
        &opts.input.to_string_lossy(),
        DRIVER_CRATE_NAME,
        &opts.entry,
        &shared_libs,
        &opts.rustc_passthrough,
    )
    .map_err(|err| err.to_string())
}

fn main() {
    match run() {
        Ok(code) => process::exit(code),
        Err(err) if err == usage() => {
            eprintln!("{err}");
            process::exit(2);
        }
        Err(err) => {
            eprintln!("rust-run: {err}");
            process::exit(1);
        }
    }
}
