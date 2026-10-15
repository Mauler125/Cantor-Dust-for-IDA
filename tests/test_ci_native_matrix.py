"""Offline tests for the GitHub Actions native release orchestrator."""
import importlib.util
import json
import os
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

    def test_upstream_versions_are_pinned(self):
        self.assertEqual(self.deps.QT_TAG, "v6.8.2")
        self.assertEqual(set(self.deps.SDK_TAGS), {"9.2", "9.3", "9.4"})

    def test_workflow_runner_matrix_and_draft_gate(self):
        text = (ROOT / ".github/workflows/release-draft.yml").read_text()
        for runner in ("windows-2025", "windows-11-arm", "ubuntu-24.04",
                       "ubuntu-24.04-arm", "macos-15"):
            self.assertIn("runner: " + runner, text)
        self.assertIn("--macos-universal", text)
        self.assertIn("scripts/ci-verify-release.py --dist dist", text)
        self.assertIn("gh release create \"$RELEASE_TAG\" --draft", text)
        self.assertNotIn("--draft=false", text)


if __name__ == "__main__":
    unittest.main()
