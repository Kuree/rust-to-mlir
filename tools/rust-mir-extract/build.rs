use std::process::Command;

fn main() {
    let rustc = std::env::var_os("RUSTC").unwrap_or_else(|| "rustc".into());
    let output = Command::new(rustc)
        .args(["--print", "sysroot"])
        .output()
        .expect("failed to query rustc sysroot");
    let sysroot = String::from_utf8(output.stdout).expect("rustc sysroot is not UTF-8");
    let libdir = format!("{}/lib", sysroot.trim());
    println!("cargo:rustc-link-arg=-Wl,-rpath,{libdir}");
    println!("cargo:rustc-link-lib=dl");
    if let Ok(cargo) = std::env::var("RUST_TO_LLVM_CARGO_EXECUTABLE") {
        println!("cargo:rustc-env=RUST_TO_LLVM_CARGO_EXECUTABLE={cargo}");
    }
    if let Ok(rustc) = std::env::var("RUST_TO_LLVM_RUSTC_EXECUTABLE") {
        println!("cargo:rustc-env=RUST_TO_LLVM_RUSTC_EXECUTABLE={rustc}");
    }
    if let Ok(capi_library) = std::env::var("RUST_TO_LLVM_CAPI_LIBRARY") {
        println!("cargo:rustc-env=RUST_TO_LLVM_CAPI_LIBRARY={capi_library}");
    }
    if let Ok(runtime_library) = std::env::var("RUST_TO_LLVM_RUNTIME_LIBRARY") {
        println!("cargo:rustc-env=RUST_TO_LLVM_RUNTIME_LIBRARY={runtime_library}");
    }
}
