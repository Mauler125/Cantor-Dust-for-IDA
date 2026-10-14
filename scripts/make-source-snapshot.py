#!/usr/bin/env python3
"""Create a complete, reproducible ZIP from tracked project files."""

import argparse
import pathlib
import subprocess
import zipfile


REQUIRED = (
    "CMakeLists.txt",
    "scripts/build-ida.ps1",
    "scripts/build-ida.sh",
    "scripts/diagnose-qt.ps1",
    "scripts/setup-qt-ida94.ps1",
    "src/ida/ida_plugin.cpp",
)


def Run() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parent.parent
    names = subprocess.check_output(
        ["git", "ls-files", "-z"], cwd=root
    ).decode("utf-8").split("\0")
    names = sorted(name for name in names if name)
    absent = [name for name in REQUIRED if name not in names]
    if absent:
        raise RuntimeError("Required files not tracked: " + ", ".join(absent))
    for name in names:
        if not (root / name).is_file():
            raise RuntimeError("Tracked file missing: " + name)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(
        args.output, "w", compression=zipfile.ZIP_DEFLATED
    ) as archive:
        for name in names:
            archive.write(root / name, arcname="cantordust-ida/" + name)
    with zipfile.ZipFile(args.output) as archive:
        members = {name.removeprefix("cantordust-ida/")
                   for name in archive.namelist()}
        if set(names) != members or archive.testzip() is not None:
            raise RuntimeError("Source archive validation failed")
    print(f"Created {args.output}: {len(names)} tracked project files")


if __name__ == "__main__":
    Run()
