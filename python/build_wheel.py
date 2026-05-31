#!/usr/bin/env python3
import argparse
import importlib.util
import os
import shutil
import subprocess
import sys
from pathlib import Path


def main():
    args = parse_args()
    python_dir = Path(__file__).resolve().parent
    build_dir = args.build_dir.resolve()
    raw_dir = args.raw_dir.resolve()
    dist_dir = args.dist_dir.resolve()

    require_module("hatchling")
    require_module("auditwheel")

    if raw_dir.exists():
        shutil.rmtree(raw_dir)
    raw_dir.mkdir(parents=True)
    dist_dir.mkdir(parents=True, exist_ok=True)

    env = os.environ.copy()
    env.setdefault("RUST_TO_MLIR_CMAKE_BINARY_DIR", str(build_dir))
    if args.config:
        env.setdefault("RUST_TO_MLIR_CMAKE_CONFIGURATION", args.config)
    prepend_env_path(env, "PATH", str(Path(sys.executable).parent))

    old_cwd = Path.cwd()
    old_environ = os.environ.copy()
    os.chdir(python_dir)
    try:
        os.environ.update(env)
        from hatchling.build import build_wheel

        wheel_name = build_wheel(str(raw_dir))
    finally:
        os.environ.clear()
        os.environ.update(old_environ)
        os.chdir(old_cwd)

    raw_wheel = raw_dir / wheel_name
    if not raw_wheel.is_file():
        raise SystemExit(f"hatchling did not create expected wheel: {raw_wheel}")

    command = [
        sys.executable,
        "-m",
        "auditwheel",
        "repair",
        "--wheel-dir",
        str(dist_dir),
    ]
    if args.plat:
        command.extend(["--plat", args.plat])
    command.append(str(raw_wheel))
    subprocess.run(command, check=True, env=env)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Build a RustToMLIR wheel with Hatchling and repair it with auditwheel."
    )
    default_build_dir = Path(__file__).resolve().parents[1] / "build"
    parser.add_argument("--build-dir", type=Path, default=default_build_dir)
    parser.add_argument(
        "--raw-dir",
        type=Path,
        default=default_build_dir / "python-wheel",
        help="directory for the unrepaired Hatchling wheel",
    )
    parser.add_argument(
        "--dist-dir",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "dist",
        help="directory for the auditwheel-repaired wheel",
    )
    parser.add_argument("--config", default="", help="CMake multi-config configuration")
    parser.add_argument("--plat", default="", help="optional auditwheel --plat value")
    return parser.parse_args()


def require_module(name):
    if importlib.util.find_spec(name) is None:
        raise SystemExit(
            f"missing Python module '{name}'. Install it in this environment with:\n"
            f"  {sys.executable} -m pip install hatchling auditwheel patchelf"
        )


def prepend_env_path(env, key, value):
    existing = env.get(key)
    env[key] = value if not existing else value + os.pathsep + existing


if __name__ == "__main__":
    main()
