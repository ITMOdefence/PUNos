#!/usr/bin/env python3
"""Check production initramfs with disabled and explicitly enabled devices."""
import gzip
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / 'System/scripts/build-initramfs.sh'
REGISTRY = ROOT / 'System/SystemServices/platform_registry.json'
ARCHIVE = ROOT / 'System/images/punos-initramfs.cpio.gz'
ROOTFS = ROOT / 'System/rootfs/punos/bin'


def build(registry):
    env = dict(os.environ, PUNOS_REGISTRY=str(registry), PUNOS_IMAGE_MODE='production')
    result = subprocess.run([str(SCRIPT)], cwd=ROOT, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if result.returncode:
        raise AssertionError(f'initramfs build exit {result.returncode}:\n{result.stdout[-4000:]}')
    listing = subprocess.run(['cpio', '-t'], input=gzip.decompress(ARCHIVE.read_bytes()),
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
    return set(listing.stdout.decode().splitlines())


def check_no_tests(files):
    assert not any('selftest' in name or 'init-smoke' in name or
                   'bootcheck' in name or 'fake' in name for name in files), files


def main():
    defaults = build(REGISTRY)
    check_no_tests(defaults)
    assert 'punos/bin/camserv' not in defaults and 'punos/bin/IOserv' not in defaults
    with tempfile.TemporaryDirectory(prefix='punos-registry-') as folder:
        path = Path(folder) / 'platform.json'
        registry = json.loads(REGISTRY.read_text())
        for service in registry['services']:
            if service['service_id'] == 'camserv':
                service.update(enabled=True, arguments=[
                    '--device=/dev/video0', '--format=Mjpeg', '--width=320',
                    '--height=240', '--fps=30'])
            elif service['service_id'] == 'IOserv':
                service.update(enabled=True, arguments=['--device=/dev/input/event0'])
        path.write_text(json.dumps(registry))
        try:
            enabled = build(path)
            check_no_tests(enabled)
            assert 'punos/bin/camserv' in enabled and 'punos/bin/IOserv' in enabled
            for name in ('camserv', 'IOserv'):
                production = ROOT / f'System/SystemServices/{name}/build/{name}'
                assert (ROOTFS / name).read_bytes() == production.read_bytes()
        finally:
            build(REGISTRY)
    print('T-16 production image excludes tests and installs explicitly enabled device ELF files')


if __name__ == '__main__':
    main()
