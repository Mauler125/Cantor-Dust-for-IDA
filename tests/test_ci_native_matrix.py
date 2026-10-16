"""Offline tests for the GitHub Actions native release orchestrator."""
import importlib.util
import json
import os
import struct
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class NativeMatrixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.verify = load("ci_verify_release", "ci-verify-release.py")
        cls.deps = load("ci_native_deps", "ci-native-deps.py")
        cls.package = load("ci_package_plugin", "package-plugin.py")

    def test_expected_matrix(self):
        expected = self.verify.EXPECTED
        self.assertEqual(len(expected), 12)
        self.assertIn(("9.4", "windows-aarch64"), expected)
        self.assertNotIn(("9.2", "windows-aarch64"), expected)
        self.assertNotIn(("9.2", "linux-aarch64"), expected)
        self.assertIn(("9.3", "linux-aarch64"), expected)
        self.assertIn(("9.4", "macos-universal"), expected)

    def test_missing_target_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaisesRegex(ValueError, "incomplete matrix"):
                self.verify.verify(Path(temp))

    def test_full_matrix_accepted_and_duplicates_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            for version, platform in self.verify.EXPECTED:
                platforms = (["macos-x86_64", "macos-aarch64"]
                             if platform == "macos-universal" else [platform])
                file = path / f"cantordust-ida-v0.8.0-ida{version}-{platform}.zip"
                with zipfile.ZipFile(file, "w") as archive:
                    archive.writestr("cantordust-ida/ida-plugin.json", json.dumps({
                        "plugin": {"version": "0.8.0", "idaVersions": [version],
                                   "platforms": platforms}}))
            self.verify.verify(path)
            duplicate = path / "cantordust-ida-v0.8.0-ida9.4-duplicate.zip"
            with zipfile.ZipFile(duplicate, "w") as archive:
                archive.writestr("cantordust-ida/ida-plugin.json", json.dumps({
                    "plugin": {"version": "0.8.0", "idaVersions": ["9.4"],
                               "platforms": ["windows-aarch64"]}}))
            with self.assertRaisesRegex(ValueError, "duplicate"):
                self.verify.verify(path)

    def test_windows_runner_cpu_not_emulated_python(self):
        with patch.object(self.package.platform, "system", return_value="Windows"), \
             patch.object(self.package.platform, "machine", return_value="AMD64"), \
             patch.dict(os.environ, {"GITHUB_ACTIONS": "true", "RUNNER_ARCH": "ARM64"}):
            self.assertEqual(self.package.HostPlatform()[0], "windows-aarch64")

    def test_windows_arm64_qt_rejects_x64_cache(self):
        with tempfile.TemporaryDirectory() as temp:
            prefix = Path(temp)
            (prefix / "bin").mkdir()
            # A minimal PE header is sufficient to verify CPU architecture.
            for name in ("Core", "Gui", "Widgets"):
                header = bytearray(134)
                header[0:2] = b"MZ"
                struct.pack_into("<I", header, 60, 128)
                header[128:132] = b"PE\0\0"
                struct.pack_into("<H", header, 132, 0xAA64)
                (prefix / "bin" / f"Qt6{name}.dll").write_bytes(header)
            self.deps.verify_arm64_qt(prefix)
            content = bytearray((prefix / "bin/Qt6Core.dll").read_bytes())
            struct.pack_into("<H", content, 132, 0x8664)
            (prefix / "bin/Qt6Core.dll").write_bytes(content)
            with self.assertRaisesRegex(RuntimeError, "not ARM64"):
                self.deps.verify_arm64_qt(prefix)

    def test_arm64_toolchain_selected_for_both_qt_and_plugin(self):
        build_script = (ROOT / "scripts/build-all.py").read_text()
        deps_script = (ROOT / "scripts/ci-native-deps.py").read_text()
        self.assertIn("arm-Windows.toolchain.cmake", build_script)
        self.assertIn("arm-Windows.toolchain.cmake", deps_script)
        self.assertIn("CMAKE_TOOLCHAIN_FILE=", build_script)
        self.assertIn("CMAKE_TOOLCHAIN_FILE=", deps_script)
        self.assertIn('"/arm64/cl.exe"', build_script)

    def test_qt_cache_is_saved_before_native_plugin_build(self):
        workflow = (ROOT / ".github/workflows/release-draft.yml").read_text()
        restore = workflow.index("uses: actions/cache/restore@v4")
        build_qt = workflow.index("- name: Fetch SDKs and build namespaced Qt if missing")
        save_qt = workflow.index("- name: Save compiled Qt before plugin compilation")
        build_plugin = workflow.index("- name: Build and package native plugins")
        self.assertLess(restore, build_qt)
        self.assertLess(build_qt, save_qt)
        self.assertLess(save_qt, build_plugin)
        block = workflow[save_qt:build_plugin]
        self.assertIn("if: steps.qt-cache.outputs.cache-hit != 'true'", block)
        self.assertIn("uses: actions/cache/save@v4", block)
        self.assertIn("key: ${{ steps.qt-cache.outputs.cache-primary-key }}", block)
        self.assertIn("path: ${{ github.workspace }}/.ci/qt-prefix", block)
        # Regression: a failed IDA compile must never prevent Qt from being cached.
        self.assertNotIn("uses: actions/cache@v4", workflow)

    def test_macos_plugin_is_named_dylib_after_sdk_target_creation(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        target = cmake.index("ida_add_plugin(cantor_dust")
        suffix = cmake.index('set_target_properties(cantor_dust PROPERTIES SUFFIX ".dylib")')
        self.assertGreater(suffix, target)
        self.assertIn("if(APPLE)", cmake[target:suffix])
        # The macOS plugin packager deliberately wants a dylib rather than
        # CMake MODULE's default *.so even though both are Mach-O binaries.
        packager = (ROOT / "scripts/package-plugin.py").read_text()
        self.assertIn('"Darwin": ".dylib"', packager)

    def test_workflow_forces_arm64_for_plugin_not_just_qt(self):
        workflow = (ROOT / ".github/workflows/release-draft.yml").read_text()
        begin = workflow.index("- name: Build and package native plugins")
        end = workflow.index("- name: Upload installable native ZIPs", begin)
        block = workflow[begin:end]
        self.assertIn("TARGET_ARCH: ${{ matrix.arch }}", block)
        self.assertIn("extra+=(--windows-arm64)", block)
        self.assertIn('"${extra[@]}"', block)
        cmake = (ROOT / "CMakeLists.txt").read_text()
        self.assertIn('CANTORDUST_EXPECTED_TARGET_ARCH STREQUAL "ARM64"', cmake)
        self.assertIn(r'"/arm64/cl\\.exe$"', cmake)

    def test_upstream_versions_are_pinned(self):
        self.assertEqual(self.deps.QT_TAG, "v6.8.2")
        self.assertEqual(set(self.deps.SDK_TAGS), {"9.2", "9.3", "9.4"})

    def test_workflow_runner_matrix_and_draft_gate(self):
        text = (ROOT / ".github/workflows/release-draft.yml").read_text()
        for runner in ("windows-2025", "windows-11-arm", "ubuntu-24.04",
                       "ubuntu-24.04-arm", "macos-15"):
            self.assertIn("runner: " + runner, text)
        self.assertIn("--macos-universal", text)
        self.assertIn("--windows-arm64", text)
        self.assertIn("matrix.arch == 'aarch64' && 'v2'", text)
        self.assertIn("scripts/ci-verify-release.py --dist dist", text)
        self.assertIn("gh release create \"$RELEASE_TAG\" --draft", text)
        self.assertNotIn("--draft=false", text)


if __name__ == "__main__":
    unittest.main()
