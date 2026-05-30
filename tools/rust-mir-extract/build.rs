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
}
