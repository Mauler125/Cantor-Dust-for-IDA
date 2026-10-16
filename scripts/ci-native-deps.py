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


def install_windows_arm64_qt_host(qt_src, work, host_prefix, parallel):
    """Provide Qt 6.8.2 host-side moc/rcc for the ARM64 cross-Qt build.

    Qt 6 treats the SDK ARM64 MSVC toolchain as a cross compiler (even on a
    Windows ARM runner). A matching host Qt installation is mandatory. Build
    it without the ARM64 toolchain so its executables run on the runner.
    Its installation is cached alongside the target ARM64 Qt installation.
    """
    host_config = host_prefix / "lib/cmake/Qt6/Qt6Config.cmake"
    host_info = host_prefix / "lib/cmake/Qt6HostInfo/Qt6HostInfoConfig.cmake"
    if host_config.is_file() and host_info.is_file():
        print(f"Using cached Qt host tools: {host_prefix}", flush=True)
        return
    host_build = work / "qtbase-host-build"
    host_cmd = ["cmake", "-S", qt_src, "-B", host_build, "-G", "Ninja",
                "-DCMAKE_BUILD_TYPE=Release",
                f"-DCMAKE_INSTALL_PREFIX={host_prefix}",
                "-DQT_NAMESPACE=QT", "-DBUILD_SHARED_LIBS=ON",
                "-DQT_BUILD_TESTS=OFF", "-DQT_BUILD_EXAMPLES=OFF",
                "-DQT_BUILD_BENCHMARKS=OFF"]
    # No -DCMAKE_TOOLCHAIN_FILE here: this Qt supplies host-executable
    # tools, not target ARM64 libraries. The SDK ARM64 toolchain is used
    # only for the second Qt configuration.
    run(*host_cmd)
    run("cmake", "--build", host_build, "--parallel", str(parallel))
    run("cmake", "--install", host_build)
    if not host_config.is_file() or not host_info.is_file():
        raise RuntimeError(f"Qt host installation is incomplete: {host_prefix}")
    print(f"Installed Qt 6.8.2 host tools: {host_prefix}", flush=True)


def install_qt(prefix, work, parallel, universal, windows_arm64=False,
               sdk_root=None, host_prefix=None):
    cmake_config = prefix / "lib/cmake/Qt6/Qt6Config.cmake"
    if windows_arm64 and host_prefix is None:
        raise RuntimeError("--qt-host-prefix is required for Windows ARM64")
    host_config = (host_prefix / "lib/cmake/Qt6HostInfo/Qt6HostInfoConfig.cmake"
                   if windows_arm64 else None)
    if cmake_config.is_file() and (not windows_arm64 or host_config.is_file()):
        if windows_arm64:
            verify_arm64_qt(prefix)
        print(f"Using cached Qt: {prefix}")
        return
    work.mkdir(parents=True, exist_ok=True)
    qt_src = work / "qtbase"
    if not qt_src.exists():
        run("git", "clone", "--depth", "1", "--branch", QT_TAG,
            "https://github.com/qt/qtbase.git", qt_src)
    if windows_arm64:
        # This must happen before configuring the SDK ARM64 target Qt.
        # Qt's cross-compiler configuration requires QT_HOST_PATH even
        # when the ARM64 runner could execute ARM64 programs natively.
        install_windows_arm64_qt_host(qt_src, work, host_prefix, parallel)
        if cmake_config.is_file():
            # Partial cache restore: the existing target is safe to reuse,
            # provided we now also have the host-side tools for the plugin.
            verify_arm64_qt(prefix)
            print(f"Using cached Qt: {prefix}")
            return
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
        cmd.append(f"-DQT_HOST_PATH={host_prefix}")
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
    parser.add_argument("--qt-host-prefix", type=Path,
                        help="matching host Qt 6.8.2 tools installation (Windows ARM64)")
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
                   args.parallel, args.macos_universal, args.windows_arm64, root,
                   args.qt_host_prefix.resolve() if args.qt_host_prefix else None)
        return 0
    except (OSError, RuntimeError, subprocess.CalledProcessError, ValueError) as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
