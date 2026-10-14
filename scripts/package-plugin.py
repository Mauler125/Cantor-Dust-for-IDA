#!/usr/bin/env python3
"""Package an already-built native plugin for IDA's plugin directory."""

import argparse
import json
import platform
import sys
import zipfile
from pathlib import Path


def Main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("dist"))
    args = parser.parse_args()

    suffix = {"Windows": ".dll", "Linux": ".so", "Darwin": ".dylib"}.get(
        platform.system()
    )
    if suffix is None:
        parser.error("unsupported host platform")

    folder = args.stage / "plugins"
    candidates = sorted(folder.glob("cantor_dust*" + suffix))
    if len(candidates) != 1:
        parser.error(
            f"expected exactly one compiled plugin in {folder}, "
            f"found {len(candidates)}; build the SDK target first"
        )

    binary = candidates[0]
    metadata = {
        "IDAMetadataDescriptorVersion": 1,
        "plugin": {"name": "cantordust-ida", "entryPoint": binary.stem},
    }
    args.output.mkdir(parents=True, exist_ok=True)
    output = args.output / (
        f"cantordust-ida-{platform.system().lower()}-{platform.machine().lower()}.zip"
    )
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as bundle:
        bundle.write(binary, f"cantordust-ida/{binary.name}")
        bundle.writestr(
            "cantordust-ida/ida-plugin.json", json.dumps(metadata, indent=2) + "\n"
        )

    print(f"Packaged {binary} -> {output}")
    print("Unzip the cantordust-ida folder into IDA's plugins directory.")
    return 0


if __name__ == "__main__":
    sys.exit(Main())
