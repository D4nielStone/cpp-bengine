#!/usr/bin/env python3
import json
import re
import sys
from pathlib import Path


def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: create_smoke_project.py <project-directory>")

    repository = Path(__file__).resolve().parents[1]
    output = Path(sys.argv[1]).resolve()
    cmake = (repository / "CMakeLists.txt").read_text(encoding="utf-8")
    version = re.search(r"VERSION\s+(\d+\.\d+\.\d+)", cmake)
    if version is None:
        raise RuntimeError("Could not read the engine version from CMakeLists.txt")

    output.mkdir(parents=True, exist_ok=True)
    project = {
        "format": "bubble-engine-project",
        "version": 2,
        "engine_version": version.group(1),
        "minimum_engine_version": version.group(1),
        "project_version": "0.1.0",
        "settings": {"gravity": [0.0, -9.8, 0.0]},
        "scene": "main.scene",
    }
    scene = {
        "format": "bubble-engine-scene",
        "version": 1,
        "entities": [],
    }
    (output / "project.bengine").write_text(
        json.dumps(project, indent=2) + "\n", encoding="utf-8"
    )
    (output / "main.scene").write_text(
        json.dumps(scene, indent=2) + "\n", encoding="utf-8"
    )


if __name__ == "__main__":
    main()
