"""Native plugin release packaging checks; requires no IDA SDK or Qt."""
import importlib.util
import json
import struct
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("plugin_package", ROOT / "scripts/package-plugin.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class PluginPackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.path = Path(self.temporary.name)
        self.meta = json.loads((ROOT / "ida-plugin.json").read_text(encoding="utf-8"))
        # The template requires the author to provide a genuine public contact email.
        self.meta["plugin"]["authors"][0]["email"] = "publisher@example.org"
        self.version = self.meta["plugin"]["version"]
        self.manifest = self.path / "ida-plugin.json"
        self.manifest.write_text(json.dumps(self.meta), encoding="utf-8")

    def test_manifest_and_release_tag(self):
        self.assertEqual(module.ValidateManifest(self.manifest, "v" + self.version)["plugin"]["version"], self.version)
        with self.assertRaises(AssertionError):
            module.ValidateManifest(self.manifest, "v999.0.0")

    def test_unconfigured_email_blocks_publication(self):
        invalid = json.loads(self.manifest.read_text(encoding="utf-8"))
        invalid["plugin"]["authors"][0]["email"] = "REPLACE_WITH_PUBLIC_EMAIL@example.invalid"
        self.manifest.write_text(json.dumps(invalid), encoding="utf-8")
        with self.assertRaisesRegex(AssertionError, "public contact email"):
            module.ValidateManifest(self.manifest)

    def test_windows_archive_and_metadata(self):
        folder = self.path / "stage/plugins"
        folder.mkdir(parents=True)
        pe = bytearray(128)
        pe[0:2] = b"MZ"
        struct.pack_into("<I", pe, 60, 64)
        pe[64:68] = b"PE\0\0"
        struct.pack_into("<H", pe, 68, 0x8664)
        (folder / "cantor_dust.dll").write_bytes(pe)
        with patch.object(module, "HostPlatform", return_value=("windows-x86_64", ".dll")):
            archive = module.Package(self.path / "stage", self.path / "dist", self.manifest, "v" + self.version, ida_version="9.2")
        import zipfile
        with zipfile.ZipFile(archive) as bundle:
            self.assertEqual(bundle.testzip(), None)
            self.assertIn("cantordust-ida/cantor_dust.dll", bundle.namelist())
            self.assertIn("cantordust-ida/LICENSE", bundle.namelist())
            metadata = json.loads(bundle.read("cantordust-ida/ida-plugin.json"))
            self.assertEqual(metadata["plugin"]["platforms"], ["windows-x86_64"])
            self.assertEqual(metadata["plugin"]["entryPoint"], "cantor_dust")
            self.assertEqual(metadata["plugin"]["idaVersions"], ["9.2"])

    def test_linux_archive(self):
        folder = self.path / "stage/plugins"
        folder.mkdir(parents=True)
        elf = bytearray(64)
        elf[0:6] = b"\x7fELF\x02\x01"
        struct.pack_into("<H", elf, 18, 62)
        (folder / "cantor_dust.so").write_bytes(elf)
        with patch.object(module, "HostPlatform", return_value=("linux-x86_64", ".so")):
            archive = module.Package(self.path / "stage", self.path / "dist", self.manifest, "v" + self.version, ida_version="9.2")
        import zipfile
        with zipfile.ZipFile(archive) as bundle:
            self.assertIn("cantordust-ida/cantor_dust.so", bundle.namelist())
            metadata = json.loads(bundle.read("cantordust-ida/ida-plugin.json"))
            self.assertEqual(metadata["plugin"]["platforms"], ["linux-x86_64"])

    def test_version_required_for_multi_version_manifest(self):
        with self.assertRaisesRegex(ValueError, "--ida-version"):
            module.Package(self.path / "stage", self.path / "dist", self.manifest)

    def test_version_restriction(self):
        with self.assertRaisesRegex(ValueError, "not supported"):
            module.Package(self.path / "stage", self.path / "dist", self.manifest,
                           ida_version="8.4")

    def test_wrong_cpu_rejected(self):
        folder = self.path / "stage/plugins"
        folder.mkdir(parents=True)
        elf = bytearray(64)
        elf[0:6] = b"\x7fELF\x02\x01"
        struct.pack_into("<H", elf, 18, 183)  # ARM64
        (folder / "cantor_dust.so").write_bytes(elf)
        with patch.object(module, "HostPlatform", return_value=("linux-x86_64", ".so")):
            with self.assertRaisesRegex(ValueError, "CPU mismatch"):
                module.Package(self.path / "stage", self.path / "dist",
                               self.manifest, ida_version="9.2")

    def test_macos_universal_archive(self):
        from unittest.mock import Mock
        folder = self.path / "stage/plugins"
        folder.mkdir(parents=True)
        (folder / "cantor_dust.dylib").write_bytes(b"universal-test")
        with patch.object(module, "HostPlatform", return_value=("macos-aarch64", ".dylib")), \
             patch("subprocess.run", return_value=Mock(stdout="x86_64 arm64\n")):
            archive = module.Package(self.path / "stage", self.path / "dist",
                                     self.manifest, ida_version="9.3")
        import zipfile
        with zipfile.ZipFile(archive) as bundle:
            meta = json.loads(bundle.read("cantordust-ida/ida-plugin.json"))["plugin"]
            self.assertEqual(meta["platforms"], ["macos-x86_64", "macos-aarch64"])
            self.assertEqual(meta["idaVersions"], ["9.3"])
            self.assertIn("macos-universal", archive.name)

    def test_macos_thin_binary_rejected(self):
        from unittest.mock import Mock
        folder = self.path / "stage/plugins"
        folder.mkdir(parents=True)
        (folder / "cantor_dust.dylib").write_bytes(b"thin-test")
        with patch.object(module, "HostPlatform", return_value=("macos-aarch64", ".dylib")), \
             patch("subprocess.run", return_value=Mock(stdout="arm64\n")):
            with self.assertRaisesRegex(ValueError, "universal dylib"):
                module.Package(self.path / "stage", self.path / "dist",
                               self.manifest, ida_version="9.3")

    def test_missing_binary_is_error(self):
        (self.path / "stage/plugins").mkdir(parents=True)
        with patch.object(module, "HostPlatform", return_value=("windows-x86_64", ".dll")):
            with self.assertRaisesRegex(ValueError, "compiled Cantor Dust plugin"):
                module.Package(self.path / "stage", self.path / "dist", self.manifest, ida_version="9.2")


if __name__ == "__main__":
    unittest.main()
