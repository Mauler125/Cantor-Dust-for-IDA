"""Multi-version release orchestrator checks. No SDK/Qt needed for ZIP tests."""
import importlib.util
import json
import io
from contextlib import redirect_stdout
from unittest.mock import patch
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("build_all", ROOT / "scripts/build-all.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class MultiVersionBuildTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_legacy_sdk_version_detection(self):
        for version, code in (("9.2", 920), ("9.3", 930), ("9.4", 940)):
            path = self.root / ("sdk" + version) / "src" / "include"
            path.mkdir(parents=True)
            (path / "pro.h").write_text(f"#define IDA_SDK_VERSION  {code}\n")
            self.assertEqual(module.sdk_version(module.sdk_source(path.parent.parent)),
                             version)

    def test_reject_invalid_sdk(self):
        with self.assertRaisesRegex(ValueError, "invalid SDK"):
            module.sdk_source(self.root)

    def test_windows_arm64_dry_run_uses_native_sdk_toolchain(self):
        from types import SimpleNamespace
        source = self.root / "sdk" / "src"
        (source / "include").mkdir(parents=True)
        (source / "include/pro.h").write_text("#define IDA_SDK_VERSION 940\n")
        (source / "cmake/toolchains").mkdir(parents=True)
        (source / "cmake/idasdk_init.cmake").write_text("# newer SDK\n")
        (source / "cmake/toolchains/arm-Windows.toolchain.cmake").write_text(
            "set(CMAKE_SYSTEM_PROCESSOR ARM64)\n")
        qt = self.root / "qt"
        (qt / "lib/cmake/Qt6").mkdir(parents=True)
        (qt / "lib/cmake/Qt6/Qt6Config.cmake").touch()
        stub = SimpleNamespace(HostPlatform=lambda: ("windows-aarch64", ".dll"),
                               ValidateManifest=lambda _: {"plugin": {"version": "0.8.0"}})
        log = io.StringIO()
        with patch.object(module, "plugin_package", return_value=stub), \
             redirect_stdout(log):
            rc = module.main(["--dry-run", "--windows-arm64", "--sdk", f"9.4={source.parent}",
                              "--versions", "9.4", "--qt-prefix", str(qt)])
        self.assertEqual(rc, 0, log.getvalue())
        self.assertIn(f"-DCMAKE_TOOLCHAIN_FILE={source / 'cmake/toolchains/arm-Windows.toolchain.cmake'}",
                      log.getvalue())
        self.assertIn("-DCANTORDUST_EXPECTED_TARGET_ARCH=ARM64", log.getvalue())

    def test_arm64_flag_rejects_non_arm64_host(self):
        from types import SimpleNamespace
        stub = SimpleNamespace(HostPlatform=lambda: ("windows-x86_64", ".dll"),
                               ValidateManifest=lambda _: {"plugin": {"version": "0.8.0"}})
        with patch.object(module, "plugin_package", return_value=stub):
            self.assertEqual(module.main(["--dry-run", "--windows-arm64",
                                          "--versions", "9.4",
                                          "--qt-prefix", str(self.root)]), 1)

    def test_collect_platform_assets(self):
        archives = []
        for index, (ida, platform_name) in enumerate((("9.2", "windows-x86_64"),
                                                       ("9.3", "linux-x86_64"))):
            path = self.root / ("machine" + str(index))
            path.mkdir()
            asset = path / f"cantordust-ida-v0.8.0-ida{ida}-{platform_name}.zip"
            metadata = {"plugin": {"version": "0.8.0",
                                   "idaVersions": [ida],
                                   "platforms": [platform_name]}}
            with zipfile.ZipFile(asset, "w") as archive:
                archive.writestr("cantordust-ida/ida-plugin.json", json.dumps(metadata))
                archive.writestr("cantordust-ida/cantor_dust.dll", b"plugin")
            archives.append(path)
        module.collect(archives, self.root / "release")
        self.assertEqual(len(list((self.root / "release").glob("*.zip"))), 2)
        with self.assertRaisesRegex(ValueError, "duplicate release target"):
            module.collect([archives[0], archives[0]], self.root / "duplicate")


if __name__ == "__main__":
    unittest.main()
