# -*- Python -*-

import os
import shlex

import lit.formats
from lit.llvm import llvm_config

config.name = "RUST_TO_LLVM"
config.test_format = lit.formats.ShTest(not llvm_config.use_lit_shell)
config.suffixes = [".mlir", ".ndjson", ".rs"]
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = os.path.join(config.rust_to_llvm_obj_root, "test")

config.excludes = ["Inputs", "CMakeLists.txt", "README.txt", "LICENSE.txt"]

llvm_config.with_system_environment(["HOME", "TMP", "TEMP"])
llvm_config.use_default_substitutions()

config.rust_to_llvm_tools_dir = os.path.join(config.rust_to_llvm_obj_root, "bin")
tool_dirs = [config.rust_to_llvm_tools_dir, config.llvm_tools_dir]
tools = ["rust-translate", "rust-opt", "FileCheck", "not"]
llvm_config.add_tool_substitutions(tools, tool_dirs)

config.substitutions.append(
    ("%rust_mir_extract", os.path.join(config.rust_to_llvm_tools_dir, "rust-mir-extract"))
)

if config.rust_to_llvm_cargo_executable:
    config.environment["CARGO"] = config.rust_to_llvm_cargo_executable
    config.substitutions.append(("%cargo", shlex.quote(config.rust_to_llvm_cargo_executable)))

if config.rust_to_llvm_rustc_executable:
    config.environment["RUSTC"] = config.rust_to_llvm_rustc_executable
    config.substitutions.append(("%rustc", shlex.quote(config.rust_to_llvm_rustc_executable)))
