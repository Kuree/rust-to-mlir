import os
import sys
import sysconfig
from pathlib import Path

from hatchling.builders.hooks.plugin.interface import BuildHookInterface


class CustomBuildHook(BuildHookInterface):
    def initialize(self, version, build_data):
        if self.target_name != "wheel":
            return

        root = Path(self.root)
        build_dir = Path(
            os.environ.get("RUST_TO_MLIR_CMAKE_BINARY_DIR", root.parent / "build")
        ).resolve()
        config = os.environ.get("RUST_TO_MLIR_CMAKE_CONFIGURATION", "")

        build_data["pure_python"] = False
        build_data["tag"] = os.environ.get(
            "RUST_TO_MLIR_WHEEL_TAG", f"py3-none-{platform_tag()}"
        )

        files = []
        for binary in ("rust-cpu-runner", "rust-mir-extract", "rust-opt", "rust-run"):
            source = find_artifact(build_dir, "bin", binary + executable_suffix(), config)
            files.append((source, f"rust_to_mlir/_native/bin/{source.name}"))

        for library in (
            shared_library_filename("RustToMLIRRustCAPI"),
            shared_library_filename("rust_to_mlir_runtime"),
        ):
            source = find_artifact(build_dir, "lib", library, config)
            files.append((source, f"rust_to_mlir/_native/lib/{source.name}"))

        missing = [str(source) for source, _ in files if not source.is_file()]
        if missing:
            formatted = "\n  ".join(missing)
            raise RuntimeError(
                "missing native artifacts for wheel build:\n"
                f"  {formatted}\n"
                "Build the native targets first or set RUST_TO_MLIR_CMAKE_BINARY_DIR."
            )

        force_include = build_data.setdefault("force_include", {})
        for source, wheel_path in files:
            force_include[str(source)] = wheel_path


def executable_suffix():
    return ".exe" if os.name == "nt" else ""


def shared_library_filename(stem):
    if os.name == "nt":
        return f"{stem}.dll"
    if sys.platform == "darwin":
        return f"lib{stem}.dylib"
    return f"lib{stem}.so"


def platform_tag():
    return sysconfig.get_platform().replace("-", "_").replace(".", "_")


def find_artifact(build_dir, subdir, filename, config):
    candidates = []
    if config:
        candidates.append(build_dir / subdir / config / filename)
    candidates.append(build_dir / subdir / filename)

    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return candidates[0]
