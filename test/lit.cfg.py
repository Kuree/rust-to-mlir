# -*- Python -*-

import os
import shlex

import lit.formats
from lit.llvm import llvm_config
from lit.llvm.subst import FindTool, ToolSubst

loaded_site_config = False
if not hasattr(config, "rust_to_mlir_obj_root"):
    site_config = os.path.join(os.getcwd(), "lit.site.cfg.py")
    if not os.path.exists(site_config):
        lit_config.fatal("missing configured lit.site.cfg.py for RustToMLIR tests")
    lit_config.load_config(config, site_config)
    loaded_site_config = True

if not loaded_site_config:
    config.name = "RUST_TO_MLIR"
    config.test_format = lit.formats.ShTest(not llvm_config.use_lit_shell)
    config.suffixes = [".mlir", ".rs"]
    config.test_source_root = os.path.dirname(__file__)
    config.test_exec_root = os.path.join(config.rust_to_mlir_obj_root, "test")

    config.excludes = ["Inputs", "CMakeLists.txt", "README.txt", "LICENSE.txt"]

    llvm_config.with_system_environment(["HOME", "TMP", "TEMP"])
    llvm_config.use_default_substitutions()

    config.rust_to_mlir_tools_dir = os.path.join(config.rust_to_mlir_obj_root, "bin")
    rust_to_mlir_tools = [
        ToolSubst("rust-opt", FindTool("rust-opt"), unresolved="fatal"),
        ToolSubst("rust-cpu-runner", FindTool("rust-cpu-runner"), unresolved="fatal"),
        ToolSubst("rust-run", FindTool("rust-run"), unresolved="fatal"),
    ]
    llvm_config.add_tool_substitutions(rust_to_mlir_tools, [config.rust_to_mlir_tools_dir])
    llvm_config.add_tool_substitutions(["FileCheck", "not"], [config.llvm_tools_dir])

    rust_mir_extract = os.path.join(config.rust_to_mlir_tools_dir, "rust-mir-extract")
    config.substitutions.append(("%rust_mir_extract", rust_mir_extract))

    if config.rust_to_mlir_cargo_executable:
        config.environment["CARGO"] = config.rust_to_mlir_cargo_executable
        config.substitutions.append(("%cargo", shlex.quote(config.rust_to_mlir_cargo_executable)))

    if config.rust_to_mlir_rustc_executable:
        config.environment["RUSTC"] = config.rust_to_mlir_rustc_executable
        config.substitutions.append(("%rustc", shlex.quote(config.rust_to_mlir_rustc_executable)))
