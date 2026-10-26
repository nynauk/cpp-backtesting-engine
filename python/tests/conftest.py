import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "python"))


def find_binary(name: str) -> Path | None:
    """The engine's executables: $BT_BUILD_DIR, then build/ (Ninja/Make), build/Release (Visual Studio) or
    cmake-build-release/ (CLion)."""
    suffix = ".exe" if os.name == "nt" else ""
    roots = [Path(os.environ["BT_BUILD_DIR"])] if "BT_BUILD_DIR" in os.environ else []
    roots += [ROOT / "build", ROOT / "build" / "Release", ROOT / "cmake-build-release"]
    for root in roots:
        candidate = root / f"{name}{suffix}"
        if candidate.exists():
            return candidate
    return None
