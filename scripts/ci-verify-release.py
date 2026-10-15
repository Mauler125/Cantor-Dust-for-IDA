#!/usr/bin/env python3
"""Fail closed when a CI release is missing an IDA/OS/CPU target."""
import argparse
import json
import sys
import zipfile
from pathlib import Path

# Older SDKs have no Windows ARM64 or Linux ARM64 link libraries.
# Universal macOS binaries cover two architectures in one package.
EXPECTED = {
    (version, platform)
    for platform, versions in {
        'windows-x86_64': ('9.2', '9.3', '9.4'),
        'windows-aarch64': ('9.4',),
        'linux-x86_64': ('9.2', '9.3', '9.4'),
        'linux-aarch64': ('9.3', '9.4'),
        'macos-universal': ('9.2', '9.3', '9.4'),
    }.items() for version in versions
}


def verify(dist):
    found = set()
    files = sorted(dist.glob('cantordust-ida-v*-ida*-*.zip'))
    for path in files:
        with zipfile.ZipFile(path) as archive:
            if archive.testzip() is not None:
                raise ValueError(f'corrupt ZIP: {path}')
            names = [p for p in archive.namelist() if p.endswith('/ida-plugin.json')]
            if len(names) != 1:
                raise ValueError(f'expected exactly one manifest: {path}')
            metadata = json.loads(archive.read(names[0]))['plugin']
            if len(metadata['idaVersions']) != 1:
                raise ValueError(f'expected exactly one IDA version: {path}')
            version = metadata['idaVersions'][0]
            platforms = set(metadata['platforms'])
            if platforms == {'macos-x86_64', 'macos-aarch64'}:
                target = 'macos-universal'
            elif len(platforms) == 1:
                target = next(iter(platforms))
            else:
                raise ValueError(f'invalid platform set: {path}: {platforms}')
            if (version, target) in found:
                raise ValueError(f'duplicate release target: {version}/{target}')
            found.add((version, target))
    missing = EXPECTED - found
    extra = found - EXPECTED
    if missing or extra:
        raise ValueError(f'incomplete matrix: missing={sorted(missing)} extra={sorted(extra)}')
    print(f'Validated all {len(EXPECTED)} native release packages.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dist', type=Path, required=True)
    args = parser.parse_args()
    try:
        verify(args.dist)
        return 0
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as e:
        print(f'ERROR: {e}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
