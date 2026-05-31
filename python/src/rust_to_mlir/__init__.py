"""Python entry points for the RustToMLIR native tools."""

from importlib.resources import files

from ._paths import native_executable, native_library

__all__ = ["native_executable", "native_library"]
__version__ = files(__package__).joinpath("VERSION").read_text(encoding="utf-8").strip()
