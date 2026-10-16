#!/usr/bin/env python3
"""Prepare public IDA SDK checkouts and a namespaced QtBase on a native CI runner.

The GitHub Actions job caches the installed Qt prefix. Native SDKs are checked
out from pinned upstream release tags. No SDK/Qt binaries are shipped in this
repository or in the finished plugin archives.
"""
import argparse
import struct
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


def verify_arm64_qt(prefix):
    """A cached x64 Qt import library cannot satisfy ARM64 plugin symbols."""
    for module in ("Core", "Gui", "Widgets"):
        binary = prefix / "bin" / f"Qt6{module}.dll"
        if not binary.is_file():
            raise RuntimeError(f"Windows ARM64 Qt DLL missing: {binary}")
        with binary.open("rb") as stream:
            header = stream.read(64)
            if len(header) != 64 or header[:2] != b"MZ":
                raise RuntimeError(f"Invalid Qt PE binary: {binary}")
            offset = struct.unpack_from("<I", header, 60)[0]
            if offset > 1024 * 1024:
                raise RuntimeError(f"Invalid Qt PE header offset: {binary}")
            stream.seek(offset)
            signature = stream.read(6)
            if signature[:4] != b"PE\x00\x00" or len(signature) < 6:
                raise RuntimeError(f"Invalid Qt PE signature: {binary}")
            machine = struct.unpack_from("<H", signature, 4)[0]
            if machine != 0xAA64:
                raise RuntimeError(f"Qt {module} is not ARM64 (PE machine "
                    f"0x{machine:04x}): {binary}. Rebuild the Qt cache.")
    print("Verified Qt Core/Gui/Widgets: Windows ARM64", flush=True)


def install_qt(prefix, work, parallel, universal, windows_arm64=False,
               sdk_root=None):
    cmake_config = prefix / "lib/cmake/Qt6/Qt6Config.cmake"
    if cmake_config.is_file():
        if windows_arm64:
            verify_arm64_qt(prefix)
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
    if windows_arm64:
        # Select the same ARM64 toolchain as the IDA plugin, instead of
        # allowing CMake to pick emulated x64 cl.exe on this runner.
        if sdk_root is None:
            raise RuntimeError("SDK root required for Windows ARM64 Qt build")
        toolchain = sdk_root / "ida-sdk-9.4/src/cmake/toolchains/arm-Windows.toolchain.cmake"
        if not toolchain.is_file():
            raise RuntimeError(f"Windows ARM64 toolchain missing: {toolchain}")
        cmd.append(f"-DCMAKE_TOOLCHAIN_FILE={toolchain}")
    run(*cmd)
    run("cmake", "--build", qt_build, "--parallel", str(parallel))
    run("cmake", "--install", qt_build)
    if not cmake_config.is_file():
        raise RuntimeError(f"Qt installation missing {cmake_config}")
    if windows_arm64:
        verify_arm64_qt(prefix)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-root", type=Path, required=True)
    parser.add_argument("--qt-prefix", type=Path, required=True)
    parser.add_argument("--qt-work", type=Path, required=True)
    parser.add_argument("--versions", nargs="+", choices=SDK_TAGS, required=True)
    parser.add_argument("--parallel", type=int, default=min(4, os.cpu_count() or 2))
    parser.add_argument("--macos-universal", action="store_true")
    parser.add_argument("--windows-arm64", action="store_true",
                        help="use the SDK ARM64 MSVC toolchain for Qt")
    args = parser.parse_args()
    try:
        if args.parallel < 1:
            raise ValueError("--parallel must be positive")
        if args.macos_universal and args.windows_arm64:
            raise ValueError("macOS universal and Windows ARM64 are separate targets")
        if args.windows_arm64 and "9.4" not in args.versions:
            raise ValueError("Windows ARM64 requires the 9.4 SDK")
        root = args.sdk_root.resolve()
        install_sdks(root, args.versions)
        install_qt(args.qt_prefix.resolve(), args.qt_work.resolve(),
                   args.parallel, args.macos_universal, args.windows_arm64, root)
        return 0
    except (OSError, RuntimeError, subprocess.CalledProcessError, ValueError) as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
