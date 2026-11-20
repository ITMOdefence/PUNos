"""Check launch arguments with a recording QEMU executable, without booting a guest."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / 'run-qemu-initramfs.sh'


class PlatformLaunchTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.record = self.root / 'arguments.json'
        qemu = self.root / 'qemu-system-x86_64'
        qemu.write_text(
            '#!/usr/bin/env python3\n'
            'import json, os, sys\n'
            'from pathlib import Path\n'
            'Path(os.environ["QEMU_RECORD"]).write_text(json.dumps(sys.argv[1:]))\n'
        )
        qemu.chmod(0o755)
        self.kernel = self.root / 'kernel image'
        self.initramfs = self.root / 'initramfs image'
        self.kernel.touch()
        self.initramfs.touch()
        self.env = {key: value for key, value in os.environ.items()
                    if not key.startswith('PUNOS_') and key != 'INIT_PATH'}
        self.env.update(
            PATH=str(self.root) + os.pathsep + os.environ['PATH'],
            QEMU_RECORD=str(self.record),
            PUNOS_KERNEL_IMAGE=str(self.kernel),
            PUNOS_INITRAMFS_IMAGE=str(self.initramfs),
        )

    def launch(self, **options):
        return subprocess.run(['bash', str(SCRIPT)], env=dict(self.env, **options),
                              capture_output=True, text=True, timeout=10)

    def test_default_launch_preserves_console_boot(self):
        result = self.launch()
        self.assertEqual(result.returncode, 0, result.stderr)
        args = json.loads(self.record.read_text())
        self.assertIn('-nographic', args)
        self.assertNotIn('-drive', args)
        self.assertNotIn('-nic', args)
        self.assertEqual(args[args.index('-kernel') + 1], str(self.kernel))
        self.assertNotIn('punos.data=', args[args.index('-append') + 1])

    def test_persistent_disk_and_external_port_forward(self):
        image = self.root / 'data, with spaces.img'
        image.touch()
        result = self.launch(PUNOS_DATA_IMAGE=str(image), PUNOS_HOST_PORT='19000',
                             PUNOS_GUEST_PORT='9000', PUNOS_HOST_ADDRESS='0.0.0.0')
        self.assertEqual(result.returncode, 0, result.stderr)
        args = json.loads(self.record.read_text())
        self.assertEqual(args[args.index('-drive') + 1],
                         'file=' + str(image).replace(',', ',,') + ',format=raw,if=virtio')
        self.assertIn('hostfwd=tcp:0.0.0.0:19000-:9000', args[args.index('-nic') + 1])
        command_line = args[args.index('-append') + 1]
        self.assertIn('punos.data=/dev/vda', command_line)
        self.assertIn('ip=10.0.2.15::10.0.2.2:255.255.255.0::eth0:off', command_line)

    def test_forward_defaults_to_loopback_and_same_guest_port(self):
        result = self.launch(PUNOS_HOST_PORT='9000')
        self.assertEqual(result.returncode, 0, result.stderr)
        args = json.loads(self.record.read_text())
        self.assertIn('hostfwd=tcp:127.0.0.1:9000-:9000', args[args.index('-nic') + 1])

    def test_invalid_options_do_not_launch_qemu(self):
        options = [
            {'PUNOS_HOST_PORT': value} for value in ('0', '65536', '-1', 'abc', '01', '999999999999999')
        ] + [
            {'PUNOS_GUEST_PORT': '9000'},
            {'PUNOS_HOST_PORT': '9000', 'PUNOS_GUEST_PORT': '0'},
            {'PUNOS_HOST_PORT': '9000', 'PUNOS_HOST_ADDRESS': '256.0.0.1'},
            {'PUNOS_HOST_PORT': '9000', 'PUNOS_HOST_ADDRESS': '127.0.0.1,extra'},
            {'PUNOS_DATA_IMAGE': str(self.root / 'absent.img')},
        ]
        for option in options:
            with self.subTest(option=option):
                result = self.launch(**option)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(self.record.exists())


if __name__ == '__main__':
    unittest.main()
