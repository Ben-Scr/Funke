"""Rebuild authored content without touching runtime measurements or user results."""
import runpy
from pathlib import Path

runpy.run_path(str(Path(__file__).with_name("build_ui.py")), run_name="__main__")
