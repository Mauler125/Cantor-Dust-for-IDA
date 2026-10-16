#!/usr/bin/env python3
"""One command per native OS to build and package Cantor Dust for IDA 9.2+.

Usage:
  python scripts/build-all.py --sdk-root /opt/ida-sdks --qt-prefix /opt/qt-ida-6.8.2
  python scripts/build-all.py --sdk 9.2=/opt/sdk92 --sdk 9.3=/opt/sdk931 \
      --sdk 9.4=/opt/sdk94 --qt-prefix /opt/qt-ida-6.8.2
  python scripts/build-all.py --collect artifacts/windows artifacts/linux artifacts/macos

Each OS builds native binaries on that OS. --collect merges already built ZIP
assets; it never represents cross-platform targets as locally compiled.
"""
import argparse
import importlib.util
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SUPPORTED = ("9.2", "9.3", "9.4")
SDK_FOLDER_NAMES = {
    "9.2": ("ida-sdk-9.2", "sdk-9.2", "9.2"),
    "9.3": ("ida-sdk-9.3", "ida-sdk-9.3.1-sdk.1", "sdk-9.3", "9.3"),
    "9.4": ("ida-sdk-9.4", "sdk-9.4", "9.4"),
}


def plugin_package():
    spec = importlib.util.spec_from_file_location(
        "cantordust_plugin_package", ROOT / "scripts" / "package-plugin.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def sdk_source(directory: Path) -> Path:
    for source in (directory, directory / "src"):
        if (source / "include" / "pro.h").is_file():
            return source
    raise ValueError(f"invalid SDK directory: {directory} (no include/pro.h)")


def sdk_version(source: Path) -> str:
    text = (source / "include" / "pro.h").read_text(encoding="utf-8", errors="replace")
    match = re.search(r"^\s*#define\s+IDA_SDK_VERSION\s+(\d+)", text, re.M)
    if not match:
        raise ValueError(f"missing IDA_SDK_VERSION in {source}")
    number = int(match.group(1))
    return f"{number // 100}.{number % 100 // 10}"


def resolve_sdks(args, versions):
    result = {}
    for value in args.sdk:
        if "=" not in value:
            raise ValueError("--sdk requires VERSION=PATH")
        version, path = value.split("=", 1)
        if version not in SUPPORTED or version in result:
            raise ValueError(f"unsupported or duplicate --sdk version: {version}")
        result[version] = Path(path).expanduser().resolve()
    for version in versions:
        if version not in result and args.sdk_root:
            for name in SDK_FOLDER_NAMES[version]:
                candidate = args.sdk_root / name
                if candidate.is_dir():
                    result[version] = candidate.resolve()
                    break
        if version not in result:
            raise ValueError(f"IDA {version} SDK missing. Pass --sdk {version}=PATH "
                             "or put the extracted SDK under --sdk-root")
        actual = sdk_version(sdk_source(result[version]))
        if actual != version:
            raise ValueError(f"{result[version]} reports SDK {actual}, expected {version}")
    return result


def run(command, dry=False):
    print("+", " ".join(map(str, command)), flush=True)
    if not dry:
        subprocess.run([str(arg) for arg in command], check=True)


def verify_target_environment(source: Path, qt: Path, platform_id: str):
    if not (qt / "lib/cmake/Qt6/Qt6Config.cmake").is_file():
        raise ValueError(f"Qt6Config.cmake is missing from Qt prefix {qt}")
    modern = (source / "cmake/idasdk_init.cmake").is_file()
    if not modern and platform_id in ("windows-aarch64", "linux-aarch64"):
        raise ValueError("SDK 9.2/9.3 does not supply native ARM64 import libraries "
                         "on this platform; use an SDK with matching native libraries")
    return modern


def collect(paths, output):
    output.mkdir(parents=True, exist_ok=True)
    seen = {}
    total = 0
    for directory in paths:
        if not directory.is_dir():
            raise ValueError(f"missing platform artifact directory: {directory}")
        for asset in sorted(directory.glob("cantordust-ida-v*-ida*-*.zip")):
            with zipfile.ZipFile(asset) as archive:
                if archive.testzip():
                    raise ValueError(f"corrupt ZIP: {asset}")
                metadata_files = [name for name in archive.namelist()
                                  if name.endswith("/ida-plugin.json")]
                if len(metadata_files) != 1:
                    raise ValueError(f"invalid native plugin manifest: {asset}")
                meta = json.loads(archive.read(metadata_files[0]))["plugin"]
                if len(meta["idaVersions"]) != 1 or not meta["platforms"]:
                    raise ValueError(f"expected one IDA version and known platforms: {asset}")
                key = (meta["version"], meta["idaVersions"][0],
                       tuple(sorted(meta["platforms"])))
                if key in seen:
                    raise ValueError(f"duplicate release target: {asset} and {seen[key]}")
                seen[key] = asset
            destination = output / asset.name
            if destination.exists() and destination.resolve() != asset.resolve():
                raise ValueError(f"refusing to overwrite: {destination}")
            if asset.resolve() != destination.resolve():
                shutil.copy2(asset, destination)
            total += 1
    if not total:
        raise ValueError("no native release ZIP files found")
    print(f"Collected {total} validated native release assets in {output}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-root", type=Path, help="parent directory containing extracted SDKs")
    parser.add_argument("--sdk", action="append", default=[], metavar="VERSION=PATH")
    parser.add_argument("--versions", nargs="+", default=list(SUPPORTED), choices=SUPPORTED)
    parser.add_argument("--qt-prefix", type=Path, help="IDA-compatible namespaced Qt6 prefix")
    parser.add_argument("--qt-host-prefix", type=Path,
                        help="Qt 6.8.2 host tools for Windows ARM64 cross-builds")
    parser.add_argument("--build-root", type=Path, default=ROOT / "build-ida-matrix")
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    parser.add_argument("--generator", default="Ninja")
    parser.add_argument("--parallel", type=int, default=min(4, os.cpu_count() or 1))
    parser.add_argument("--cmake-arg", action="append", default=[])
    parser.add_argument("--windows-arm64", action="store_true",
                        help="require Windows ARM64 and use the SDK ARM64 toolchain")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--collect", nargs="+", type=Path,
                        help="only combine already built native packages from each OS")
    args = parser.parse_args(argv)
    try:
        if args.collect:
            collect(args.collect, args.output)
            return 0
        if not args.qt_prefix:
            raise ValueError("--qt-prefix is required (IDA namespaced Qt 6.8.2)")
        package = plugin_package()
        meta = package.ValidateManifest(ROOT / "ida-plugin.json")
        host, _ = package.HostPlatform()
        if args.windows_arm64 and host != "windows-aarch64":
            raise ValueError("--windows-arm64 requires a Windows ARM64 runner "
                             f"(detected {host})")
        if host == "windows-aarch64" and not args.windows_arm64 and            os.environ.get("GITHUB_ACTIONS") == "true":
            raise ValueError("CI Windows ARM64 builds must explicitly pass "
                             "--windows-arm64 (to enforce SDK toolchain selection)")
        versions = list(dict.fromkeys(args.versions))
        sdks = resolve_sdks(args, versions)
        qt = args.qt_prefix.expanduser().resolve()
        if not args.dry_run and not shutil.which("cmake"):
            raise ValueError("cmake not found on PATH")
        if args.generator == "Ninja" and not args.dry_run and not shutil.which("ninja"):
            raise ValueError("ninja not found on PATH")
        for version in versions:
            source = sdk_source(sdks[version])
            modern = verify_target_environment(source, qt, host)
            build = args.build_root.resolve() / f"ida-{version}"
            stage = build / "stage"
            cmake_args = ["cmake", "-S", ROOT, "-B", build, "-G", args.generator,
                          "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=OFF",
                          "-DCANTORDUST_BUILD_IDA_PLUGIN=ON",
                          f"-DCANTORDUST_TARGET_IDA_VERSION={version}",
                          f"-DIDASDK={source}", f"-DIDABIN={stage}",
                          f"-DCMAKE_PREFIX_PATH={qt}",
                          "-DCANTORDUST_EXPECTED_QT_VERSION=6.8.2"]
            if sys.platform == "darwin":
                cmake_args.append("-DCMAKE_OSX_ARCHITECTURES=x86_64;arm64")
            if host == "windows-aarch64":
                # The SDK auto-detects x64 MSVC unless this toolchain is set
                # *before* the top-level project() call. Qt was separately
                # compiled for ARM64; x64 objects cannot link against it.
                toolchain = source / "cmake/toolchains/arm-Windows.toolchain.cmake"
                if not toolchain.is_file():
                    raise ValueError(f"ARM64 SDK toolchain missing: {toolchain}")
                cmake_args.extend((f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
                                   "-DCANTORDUST_EXPECTED_TARGET_ARCH=ARM64"))
                # A cross-built Qt requires matching host-side moc/rcc tools;
                # pass the same host prefix used to build the ARM64 Qt SDK.
                host_qt = (args.qt_host_prefix.expanduser().resolve()
                           if args.qt_host_prefix else qt / "host-qt")
                if not args.dry_run and not (
                        host_qt / "lib/cmake/Qt6HostInfo/Qt6HostInfoConfig.cmake").is_file():
                    raise ValueError(f"Windows ARM64 Qt host tools missing: {host_qt}")
                cmake_args.append(f"-DQT_HOST_PATH={host_qt}")
            cmake_args.extend(args.cmake_arg)
            print(f"\nBuilding IDA {version} for {host} "
                  f"({'native SDK CMake' if modern else 'legacy SDK adapter'})")
            run(cmake_args, args.dry_run)
            if not args.dry_run and host == "windows-aarch64":
                # Detect a misconfigured compiler before the costly full build.
                compiler = next((line.split("=", 1)[1] for line in
                    (build / "CMakeCache.txt").read_text(errors="replace").splitlines()
                    if line.startswith("CMAKE_CXX_COMPILER:")), "")
                normalized = compiler.replace("\\", "/").lower()
                if not normalized.endswith("/arm64/cl.exe"):
                    raise ValueError("Windows ARM64 needs an ARM64 compiler; "
                        f"CMake configured {compiler!r}")
                # The compiler path alone is insufficient: a stale CMake
                # cache could still have x64 architecture/linker settings.
                cache = (build / "CMakeCache.txt").read_text(errors="replace")
                if "CANTORDUST_EXPECTED_TARGET_ARCH:UNINITIALIZED=ARM64" not in cache and                    "CANTORDUST_EXPECTED_TARGET_ARCH:STRING=ARM64" not in cache:
                    raise ValueError("Windows ARM64 CMake guard was not enabled")
            run(["cmake", "--build", build, "--config", "Release", "--target",
                 "cantor_dust", "--parallel", str(args.parallel)], args.dry_run)
            if not args.dry_run:
                dest = package.Package(stage, args.output.resolve(),
                    ROOT / "ida-plugin.json", ida_version=version)
                print(f"Packaged: {dest}")
        if args.dry_run:
            print("Dry run only. No binaries were compiled or packaged.")
        else:
            print(f"\nComplete: {len(versions)} native releases for {host} in {args.output}")
        return 0
    except (ValueError, OSError, AssertionError, subprocess.CalledProcessError,
            zipfile.BadZipFile) as ex:
        print(f"ERROR: {ex}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
