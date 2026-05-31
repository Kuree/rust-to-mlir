import os
import sys
from pathlib import Path


def native_root():
    return Path(__file__).resolve().parent / "_native"


def native_bin_dir():
    return native_root() / "bin"


def native_library_dir():
    return native_root() / "lib"


def native_executable(name):
    path = native_bin_dir() / (name + executable_suffix())
    if not path.is_file():
        raise FileNotFoundError(f"native executable not found: {path}")
    return path


def native_library(name):
    path = native_library_dir() / name
    if not path.is_file():
        raise FileNotFoundError(f"native library not found: {path}")
    return path


def executable_suffix():
    return ".exe" if os.name == "nt" else ""


def capi_library_filename():
    return shared_library_filename("RustToMLIRRustCAPI")


def runtime_library_filename():
    return shared_library_filename("rust_to_mlir_runtime")


def shared_library_filename(stem):
    if os.name == "nt":
        return f"{stem}.dll"
    if sys.platform == "darwin":
        return f"lib{stem}.dylib"
    return f"lib{stem}.so"
