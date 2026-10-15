#!/usr/bin/env python3
"""Validate and package a native Cantor Dust build for the IDA Plugin Manager.

The package layout and platform metadata follow the Hex-Rays HCLI format.
The source repository is not itself an installable native plugin archive.
"""

import argparse
import os
import json
import platform
import re
import struct
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SUFFIXES = {"Windows": ".dll", "Linux": ".so", "Darwin": ".dylib"}
OS_NAMES = {"Windows": "windows", "Linux": "linux", "Darwin": "macos"}
ARCH_NAMES = {
    "x86_64": "x86_64", "amd64": "x86_64", "x64": "x86_64",
    "aarch64": "aarch64", "arm64": "aarch64",
}
VALID_PLATFORMS = {
    "windows-x86_64", "windows-aarch64", "linux-x86_64",
    "linux-aarch64", "macos-x86_64", "macos-aarch64",
}
VALID_CATEGORIES = {
    "ui-ux-and-visualization", "disassembly-and-processor-modules",
    "malware-analysis", "debugging-and-tracing", "deobfuscation",
    "collaboration-and-productivity", "other", "file-parsers-and-loaders",
    "decompilation", "vulnerability-research-and-exploit-development",
    "integration-with-third-parties-interoperability", "api-scripting-and-automation",
}
VERSION = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")


def ValidateManifest(path: Path, tag: str = "") -> dict:
    """Fail closed on missing release metadata instead of publishing broken ZIPs."""
    meta = json.loads(path.read_text(encoding="utf-8"))
    assert meta.get("IDAMetadataDescriptorVersion") == 1, "invalid metadata version"
    plugin = meta.get("plugin", {})
    assert plugin.get("name") == "cantordust-ida", "incorrect plugin name"
    assert plugin.get("entryPoint") == "cantor_dust", "incorrect native entry point"
    assert VERSION.fullmatch(plugin.get("version", "")), "expected x.y.z version"
    assert plugin.get("urls", {}).get("repository", "").startswith("https://github.com/"), "missing repository URL"
    contacts = plugin.get("authors", []) or plugin.get("maintainers", [])
    assert contacts, "add at least one author or maintainer"
    for contact in contacts:
        email = contact.get("email", "")
        assert "@" in email and "." in email.split("@", 1)[1], "a contact email is required"
        assert "REPLACE" not in email.upper() and not email.endswith(".invalid"), (
            "set a real public contact email in ida-plugin.json before releasing"
        )
    assert plugin.get("idaVersions"), "specify the tested IDA versions"
    assert set(plugin.get("platforms", [])) <= VALID_PLATFORMS, "invalid platform"
    assert plugin.get("platforms"), "specify the tested platforms"
    assert set(plugin.get("categories", [])) <= VALID_CATEGORIES, "invalid category"
    if tag:
        assert tag == "v" + plugin["version"], (
            f"release tag {tag!r} does not match manifest version {plugin['version']!r}"
        )
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    found = re.search(r"project\(ida_cantordust\s+VERSION\s+(\d+\.\d+\.\d+)", cmake)
    assert found and found.group(1) == plugin["version"], (
        "CMake project version and ida-plugin.json version must match"
    )
    return meta


def HostPlatform() -> tuple[str, str]:
    system = platform.system()
    suffix = SUFFIXES.get(system)
    # Windows ARM64 runners may invoke an emulated x64 Python interpreter.
    # GitHub Actions reports the *runner* CPU independently of Python's ABI.
    runner_arch = os.environ.get("RUNNER_ARCH", "") if os.environ.get("GITHUB_ACTIONS") == "true" else ""
    arch = ARCH_NAMES.get((runner_arch or platform.machine()).lower())
    if not suffix or not arch:
        raise ValueError(f"unsupported native host: {system}/{platform.machine()}")
    name = OS_NAMES[system] + "-" + arch
    if name not in VALID_PLATFORMS:
        raise ValueError(f"unsupported platform: {name}")
    return name, suffix


def VerifyNativeHeader(path: Path, platform_id: str) -> None:
    """Refuse ZIPs that claim the wrong executable format or CPU architecture."""
    if platform_id.startswith("macos-"):
        return  # macOS universal slices are checked separately with lipo.
    with path.open("rb") as stream:
        header = stream.read(64)
        if platform_id.startswith("windows-"):
            if len(header) < 64 or header[:2] != b"MZ":
                raise ValueError(f"not a Windows PE binary: {path}")
            offset = struct.unpack_from("<I", header, 60)[0]
            if offset > 1024 * 1024:
                raise ValueError(f"invalid PE header offset: {path}")
            stream.seek(offset)
            signature = stream.read(6)
            if signature[:4] != b"PE\0\0" or len(signature) < 6:
                raise ValueError(f"invalid Windows PE header: {path}")
            machine = struct.unpack_from("<H", signature, 4)[0]
            expected = 0xAA64 if platform_id.endswith("aarch64") else 0x8664
            if machine != expected:
                raise ValueError(f"PE CPU mismatch for {platform_id}: {path}")
        elif platform_id.startswith("linux-"):
            if len(header) < 20 or header[:4] != b"\x7fELF" or header[4] != 2:
                raise ValueError(f"not a 64-bit ELF binary: {path}")
            endian = "<" if header[5] == 1 else ">" if header[5] == 2 else ""
            if not endian:
                raise ValueError(f"invalid ELF byte order: {path}")
            machine = struct.unpack_from(endian + "H", header, 18)[0]
            expected = 183 if platform_id.endswith("aarch64") else 62
            if machine != expected:
                raise ValueError(f"ELF CPU mismatch for {platform_id}: {path}")


def Package(stage: Path, output: Path, manifest: Path, target: str = "",
            ida_version: str = "", platform_id: str = "") -> Path:
    metadata = ValidateManifest(manifest, tag=target)
    native_platform, suffix = HostPlatform()
    if platform_id and platform_id != native_platform:
        raise ValueError("packaging a different host platform is not permitted")
    platform_id = native_platform
    supported = metadata["plugin"]["idaVersions"]
    if not ida_version:
        if len(supported) != 1:
            raise ValueError("--ida-version is required for a multi-version manifest")
        ida_version = supported[0]
    if ida_version not in supported:
        raise ValueError(f"IDA {ida_version} not supported by manifest: {supported}")
    folder = stage / "plugins"
    candidates = sorted(
        item for item in folder.glob("cantor_dust*" + suffix) if item.is_file()
    )
    if len(candidates) != 1:
        raise ValueError(
            f"expected exactly one compiled Cantor Dust plugin in {folder}, "
            f"found {len(candidates)} (build the native SDK target first)"
        )
    binary = candidates[0]
    VerifyNativeHeader(binary, platform_id)
    # The SDK may append an architecture marker; always use the *actual* stem.
    metadata["plugin"]["entryPoint"] = binary.stem
    package_platforms = [platform_id]
    if platform_id.startswith("macos-"):
        # HCLI currently requires a universal .dylib on macOS, rather than
        # thin arch-suffixed dylibs. Verify the binary is truly universal.
        import subprocess
        info = subprocess.run(["lipo", "-archs", str(binary)],
                              capture_output=True, text=True, check=True).stdout.split()
        if not {"x86_64", "arm64"}.issubset(info):
            raise ValueError("macOS release needs an x86_64+arm64 universal dylib")
        package_platforms = ["macos-x86_64", "macos-aarch64"]
        platform_id = "macos-universal"
    metadata["plugin"]["platforms"] = package_platforms
    metadata["plugin"]["idaVersions"] = [ida_version]
    version = metadata["plugin"]["version"]
    output.mkdir(parents=True, exist_ok=True)
    destination = output / f"cantordust-ida-v{version}-ida{ida_version}-{platform_id}.zip"
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.write(binary, f"cantordust-ida/{binary.name}")
        archive.writestr("cantordust-ida/ida-plugin.json", json.dumps(metadata, indent=2) + "\n")
        archive.write(ROOT / "LICENSE", "cantordust-ida/LICENSE")
    with zipfile.ZipFile(destination) as archive:
        assert archive.testzip() is None, "ZIP integrity failure"
        assert "cantordust-ida/" + binary.name in archive.namelist()
        packaged = json.loads(archive.read("cantordust-ida/ida-plugin.json"))
        assert packaged["plugin"]["entryPoint"] == binary.stem
        assert packaged["plugin"]["platforms"] == package_platforms
        assert packaged["plugin"]["idaVersions"] == [ida_version]
    return destination


def Main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", type=Path, help="SDK staging root containing plugins/")
    parser.add_argument("--output", type=Path, default=Path("dist"))
    parser.add_argument("--manifest", type=Path, default=ROOT / "ida-plugin.json")
    parser.add_argument("--tag", default="", help="Check release tag against manifest, e.g. v0.8.0")
    parser.add_argument("--ida-version", default="", help="IDA minor version: 9.2, 9.3, 9.4")
    parser.add_argument("--check-manifest", action="store_true", help="Validate metadata without packaging")
    args = parser.parse_args()
    try:
        if args.check_manifest:
            meta = ValidateManifest(args.manifest, args.tag)
            print(f"Validated Cantor Dust manifest v{meta['plugin']['version']}")
            return 0
        if not args.stage:
            parser.error("--stage is required when packaging a native plugin")
        dest = Package(args.stage, args.output, args.manifest, args.tag,
                       ida_version=args.ida_version)
        print(f"IDA Plugin Manager archive: {dest}")
        return 0
    except (AssertionError, ValueError, OSError, json.JSONDecodeError,
            __import__("subprocess").CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(Main())
