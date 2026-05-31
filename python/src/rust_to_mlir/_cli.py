import os
import sys

from ._paths import (
    capi_library_filename,
    native_executable,
    native_library,
    native_library_dir,
    runtime_library_filename,
)


def rust_cpu_runner():
    run_native_tool("rust-cpu-runner")


def rust_mir_extract():
    run_native_tool("rust-mir-extract")


def rust_opt():
    run_native_tool("rust-opt")


def rust_run():
    run_native_tool("rust-run")


def run_native_tool(name):
    executable = native_executable(name)
    env = os.environ.copy()
    lib_dir = str(native_library_dir())

    env.setdefault("RUST_TO_MLIR_CAPI_LIBRARY", str(native_library(capi_library_filename())))
    env.setdefault(
        "RUST_TO_MLIR_RUNTIME_LIBRARY", str(native_library(runtime_library_filename()))
    )
    prepend_env_path(env, library_path_env_var(), lib_dir)

    os.execvpe(str(executable), [str(executable), *sys.argv[1:]], env)


def library_path_env_var():
    if os.name == "nt":
        return "PATH"
    if sys.platform == "darwin":
        return "DYLD_LIBRARY_PATH"
    return "LD_LIBRARY_PATH"


def prepend_env_path(env, key, value):
    existing = env.get(key)
    env[key] = value if not existing else value + os.pathsep + existing
