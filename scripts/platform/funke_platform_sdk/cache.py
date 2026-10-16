"""Per-user cache for platform toolchain artifacts (SDL3 / Dawn / runtime packs per target)."""

from __future__ import annotations

import os
import platform
from pathlib import Path


def cache_root() -> Path:
    override = os.environ.get("FUNKE_PLATFORM_CACHE")
    if override:
        return Path(override)
    system = platform.system()
    if system == "Windows":
        base = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local"))
    elif system == "Darwin":
        base = Path.home() / "Library" / "Application Support"
    else:
        base = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local" / "share"))
    return base / "Funke" / "PlatformCache"


def platform_cache(*parts: str) -> Path:
    path = cache_root().joinpath(*parts)
    path.mkdir(parents=True, exist_ok=True)
    return path
