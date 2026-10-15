#!/usr/bin/env python3
"""Prepare public IDA SDK checkouts and a namespaced QtBase on a native CI runner.

The GitHub Actions job caches the installed Qt prefix. Native SDKs are checked
out from pinned upstream release tags. No SDK/Qt binaries are shipped in this
repository or in the finished plugin archives.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys

SDK_TAGS = {"9.2": "v9.2", "9.3": "v9.3.1-sdk.1", "9.4": "v9.4.0-sdk.1"}
QT_TAG = "v6.8.2"


def run(*command):
    print("+", " ".join(str(x) for x in command), flush=True)
    subprocess.run([str(x) for x in command], check=True)


def install_sdks(destination, versions):
    destination.mkdir(parents=True, exist_ok=True)
    for version in versions:
        path = destination / f"ida-sdk-{version}"
        if not path.exists():
            run("git", "clone", "--depth", "1", "--branch", SDK_TAGS[version],
                "https://github.com/HexRaysSA/ida-sdk.git", path)
        if not (path / "src/include/pro.h").is_file():
            raise RuntimeError(f"SDK {version} checkout lacks src/include/pro.h: {path}")
        if not (path / "src/lib").is_dir():
            raise RuntimeError(f"SDK {version} checkout lacks native libraries: {path}; "
                               "download matching SDK release assets instead")


def install_qt(prefix, work, parallel, universal):
    cmake_config = prefix / "lib/cmake/Qt6/Qt6Config.cmake"
    if cmake_config.is_file():
        print(f"Using cached Qt: {prefix}")
        return
    work.mkdir(parents=True, exist_ok=True)
    qt_src = work / "qtbase"
    if not qt_src.exists():
        run("git", "clone", "--depth", "1", "--branch", QT_TAG,
            "https://github.com/qt/qtbase.git", qt_src)
    qt_build = work / "qtbase-build"
    cmd = ["cmake", "-S", qt_src, "-B", qt_build, "-G", "Ninja",
           "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_INSTALL_PREFIX={prefix}",
           "-DQT_NAMESPACE=QT", "-DBUILD_SHARED_LIBS=ON",
           "-DQT_BUILD_TESTS=OFF", "-DQT_BUILD_EXAMPLES=OFF",
           "-DQT_BUILD_BENCHMARKS=OFF"]
    if universal:
        cmd.append("-DCMAKE_OSX_ARCHITECTURES=x86_64;arm64")
    run(*cmd)
    run("cmake", "--build", qt_build, "--parallel", str(parallel))
    run("cmake", "--install", qt_build)
    if not cmake_config.is_file():
        raise RuntimeError(f"Qt installation missing {cmake_config}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-root", type=Path, required=True)
    parser.add_argument("--qt-prefix", type=Path, required=True)
    parser.add_argument("--qt-work", type=Path, required=True)
    parser.add_argument("--versions", nargs="+", choices=SDK_TAGS, required=True)
    parser.add_argument("--parallel", type=int, default=min(4, os.cpu_count() or 2))
    parser.add_argument("--macos-universal", action="store_true")
    args = parser.parse_args()
    try:
        if args.parallel < 1:
            raise ValueError("--parallel must be positive")
        install_sdks(args.sdk_root.resolve(), args.versions)
        install_qt(args.qt_prefix.resolve(), args.qt_work.resolve(),
                   args.parallel, args.macos_universal)
        return 0
    except (OSError, RuntimeError, subprocess.CalledProcessError, ValueError) as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
