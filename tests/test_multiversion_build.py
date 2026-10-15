"""Multi-version release orchestrator checks. No SDK/Qt needed for ZIP tests."""
import importlib.util
import json
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
