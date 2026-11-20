"""Check kernel configuration gating with a recording build toolchain."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


SYSTEM = Path(__file__).resolve().parents[2]


class PlatformKernelTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.system = self.root / 'kernel project'
        scripts = self.system / 'scripts'
        configs = self.system / 'kernel/configs'
        tools = self.root / 'tools'
        for path in (scripts, configs, tools):
            path.mkdir(parents=True)
        shutil.copyfile(SYSTEM / 'scripts/build-kernel.sh', scripts / 'build-kernel.sh')
        shutil.copyfile(SYSTEM / 'kernel/configs/platform.config', configs / 'platform.config')
        (configs / 'x86_64.config').write_text('# CONFIG_MEDIA_SUPPORT is not set\n')
        (self.system / 'kernel/version').write_text('6.12.111\n')
        kernel = self.system / 'linux-6.12.111'
        (kernel / 'scripts/kconfig').mkdir(parents=True)
        (kernel / 'Makefile').touch()
        (kernel / 'scripts/kconfig/merge_config.sh').write_text(
            'set -eu\n'
            'test "$1" = -m\n'
            'test "$2" = -O\n'
            'cat "$4" "$5" > "$3/.config"\n'
        )
        make = tools / 'make'
        make.write_text(
            '#!/usr/bin/env python3\n'
            'import json, os, sys\n'
            'from pathlib import Path\n'
            'args = sys.argv[1:]\n'
            'with open(os.environ["MAKE_RECORD"], "a") as output:\n'
            '    output.write(json.dumps(args) + "\\n")\n'
            'build = Path(next(arg[2:] for arg in args if arg.startswith("O=")))\n'
            'if "olddefconfig" in args and os.environ.get("DROP_UVC"):\n'
            '    config = build / ".config"\n'
            '    config.write_text(config.read_text().replace("CONFIG_USB_VIDEO_CLASS=y", "# CONFIG_USB_VIDEO_CLASS is not set"))\n'
            'if "bzImage" in args:\n'
            '    image = build / "arch/x86/boot/bzImage"\n'
            '    image.parent.mkdir(parents=True)\n'
            '    image.touch()\n'
        )
        make.chmod(0o755)
        gcc = tools / 'gcc'
        gcc.write_text('#!/bin/sh\nexit 0\n')
        gcc.chmod(0o755)
        self.record = self.root / 'make.jsonl'
        self.env = dict(os.environ, PATH=str(tools) + os.pathsep + os.environ['PATH'],
                        MAKE_RECORD=str(self.record), JOBS='1')
        self.env.pop('DROP_UVC', None)

    def build(self, **options):
        return subprocess.run(['bash', str(self.system / 'scripts/build-kernel.sh')],
                              env=dict(self.env, **options), capture_output=True,
                              text=True, timeout=10)

    def test_platform_fragment_is_applied_before_build(self):
        result = self.build()
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = [json.loads(line) for line in self.record.read_text().splitlines()]
        self.assertIn('olddefconfig', calls[0])
        self.assertIn('bzImage', calls[1])

    def test_disabled_camera_dependency_stops_kernel_build(self):
        result = self.build(DROP_UVC='1')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('CONFIG_USB_VIDEO_CLASS=y', result.stderr)
        calls = [json.loads(line) for line in self.record.read_text().splitlines()]
        self.assertEqual(len(calls), 1)
        self.assertIn('olddefconfig', calls[0])


if __name__ == '__main__':
    unittest.main()
