#!/usr/bin/env python3
"""Black-box IPC checks of the device services with fake hardware in test ELF files."""
import json
import os
from pathlib import Path
import shutil
import signal
import socket
import struct
import subprocess
import tempfile
import time

import services_integration as existing

HERE = Path(__file__).resolve().parents[1]
ROOT = HERE.parents[1]
SYSTEM = ROOT / 'System'
FLAGS = ['-std=c11', '-ffreestanding', '-nostdinc', '-fno-builtin',
         '-fno-stack-protector', '-fno-pic', '-m64', '-Wall', '-Wextra',
         '-Werror', '-fno-tree-loop-distribute-patterns']
LIBS = ['core', 'mem', 'str', 'file', 'process', 'time', 'thread', 'sync',
        'eventloop', 'v4l2', 'inputdevice']


def build(name, output, endpoint, log_endpoint, trace, fail_loop=False):
    module = HERE / name
    includes = [module / 'include', module / 'src',
                SYSTEM / 'NativeRuntime/include',
                SYSTEM / 'NativeRuntime/SyscallWrappers/include',
                SYSTEM / 'SpecialLibs/ipclib/include',
                SYSTEM / 'SpecialLibs/loglib/include',
                SYSTEM / f'SpecialLibs/{"camlib" if name == "camserv" else "inputlib"}/include']
    includes += [SYSTEM / f'SystemLibs/lib{lib}/include' for lib in LIBS]
    options = [*FLAGS, *('-I' + str(path) for path in includes),
               f'-D{"CAM_ENDPOINT" if name == "camserv" else "IO_ENDPOINT"}="{endpoint}"',
               f'-DLOG_ENDPOINT="{log_endpoint}"', f'-DTRACE_PATH="{trace}"']
    sources = [module / 'src/main.c', module / 'src/protocol.c',
               HERE / 'common/logger.c',
               module / f'tests/fake_{"camera" if name == "camserv" else "input"}.c']
    if fail_loop:
        sources.append(HERE / 'tests/fail_eventloop.c')
    objects = []
    for index, source in enumerate(sources):
        object_path = output.parent / f'{output.name}.{index}.o'
        subprocess.run(['cc', *options, '-c', str(source), '-o', str(object_path)], check=True)
        objects.append(object_path)
    archives = [SYSTEM / f'SystemLibs/lib{lib}/build/libpunos_{lib}.a' for lib in LIBS]
    archives += [SYSTEM / 'SpecialLibs/ipclib/build/libpunos_special_ipc.a',
                 SYSTEM / 'SpecialLibs/loglib/build/libpunos_special_log.a',
                 SYSTEM / 'NativeRuntime/CRT/build/libpunos_crt.a',
                 SYSTEM / 'NativeRuntime/SyscallWrappers/build/libpunos_syscall.a']
    command = ['cc', '-nostdlib', '-no-pie', '-Wl,-e,_start',
               str(SYSTEM / 'NativeRuntime/CRT/build/crt0.o'),
               *map(str, objects)]
    if fail_loop:
        command.append('-Wl,--wrap=eventloop_create')
    subprocess.run([*command, '-Wl,--start-group', *map(str, archives),
                    '-Wl,--end-group', '-o', str(output)], check=True)
    return output


def wait(check, label, timeout=8):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if check():
            return
        time.sleep(.01)
    raise AssertionError('timeout: ' + label)


def exact(connection, size):
    data = bytearray()
    while len(data) < size:
        part = connection.recv(size - len(data))
        if not part:
            raise EOFError('connection closed')
        data.extend(part)
    return bytes(data)


def frame(kind, payload=b''):
    return struct.pack('<II', len(payload), kind) + payload


def recv(connection):
    size, kind = struct.unpack('<II', exact(connection, 8))
    assert size <= 1048576, ('oversized IPC payload', size)
    return kind, exact(connection, size)


def connect(path):
    connection = socket.socket(socket.AF_UNIX)
    connection.settimeout(5)
    connection.connect(str(path))
    return connection


def finish(process):
    if process.poll() is None:
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            raise AssertionError('SIGTERM did not terminate service')


def run_service(binary, endpoint, args):
    process = subprocess.Popen([str(binary), *args])
    wait(lambda: endpoint.exists() or process.poll() is not None, 'service startup')
    assert endpoint.exists(), ('service failed to publish endpoint', process.returncode)
    return process


def camera_args(number):
    return [f'--device=/dev/video{number}', '--format=Mjpeg',
            '--width=320', '--height=240', '--fps=30']


def cam_subscribe(connection, max_bytes=4):
    connection.sendall(frame(3, b'\x01\x00'))
    assert recv(connection) == (4, b'\0' * 4)
    kind, ready = recv(connection)
    assert kind == 7 and ready == struct.pack('<IIIII', 2, 320, 240, 30, max_bytes)


def test_camera(directory, log_endpoint, journal):
    endpoint = directory / 'camera.sock'
    trace = directory / 'camera.trace'
    binary = build('camserv', directory / 'camserv-test', endpoint, log_endpoint, trace)
    assert subprocess.run([str(binary)], timeout=5).returncode == 2
    process = run_service(binary, endpoint, camera_args(0))
    try:
        with connect(endpoint) as first, connect(endpoint) as second:
            first.sendall(frame(1))
            kind, info = recv(first)
            assert kind == 2 and info == b'\0\0\0\0\x01' + struct.pack('<IIIII', 2, 320, 240, 30, 4)
            cam_subscribe(first)
            cam_subscribe(second)
            first.sendall(frame(3, b'\x01\x00'))
            assert recv(first) == (4, struct.pack('<I', 3))
            a, b = recv(first), recv(second)
            assert a == b and a[0] == 8
            assert a[1] == struct.pack('<QQI', 1, 1001, 4) + b'\xa1\x00\xff\x5a'
            first.sendall(frame(5))
            assert recv(first) == (6, b'\0' * 4)
            first.sendall(frame(5))
            assert recv(first) == (6, struct.pack('<I', 3))
            first.sendall(frame(3, b'\x02\x00'))
            assert recv(first) == (4, struct.pack('<I', 12))
            first.sendall(frame(99))
            assert recv(first) == (10, struct.pack('<I', 1))
            first.sendall(frame(8))
            assert recv(first) == (10, struct.pack('<I', 1))
            first.sendall(frame(1, b'\0'))
            assert recv(first) == (2, struct.pack('<I', 1) + b'\0' * 21)
            second_frame = recv(second)
            assert second_frame[0] == 8 and struct.unpack_from('<Q', second_frame[1])[0] > 1
            with connect(endpoint) as malformed:
                malformed.sendall(struct.pack('<II', 1048577, 3))
                assert malformed.recv(1) == b''
            with connect(endpoint) as truncated:
                truncated.sendall(struct.pack('<II', 2, 3) + b'\x01')
            assert recv(second)[0] == 8
    finally:
        finish(process)
    assert process.returncode == 0 and not endpoint.exists()
    marks = trace.read_text().splitlines()
    assert marks.count('open') == marks.count('configure') == marks.count('start') == 1
    assert marks.count('stop') == marks.count('close') == 1
    assert marks.count('release') >= 2
    print('T-02 T-05 T-13 camera startup, subscriptions, wire bytes and SIGTERM passed')

    for number, expected in [(1, ['open']), (2, ['open', 'configure', 'close']),
                             (3, ['open', 'configure', 'close']),
                             (4, ['open', 'configure', 'start', 'close'])]:
        trace.unlink()
        process = subprocess.run([str(binary), *camera_args(number)], timeout=8)
        assert process.returncode == 3 and not endpoint.exists(), number
        assert trace.read_text().splitlines() == expected, (number, trace.read_text())
    fail_endpoint = directory / 'missing/camera.sock'
    fail_binary = build('camserv', directory / 'camserv-endpoint-fail', fail_endpoint,
                        log_endpoint, trace)
    trace.unlink()
    assert subprocess.run([str(fail_binary), *camera_args(0)], timeout=8).returncode == 3
    assert not fail_endpoint.exists()
    assert trace.read_text().splitlines() == ['open', 'configure', 'start', 'stop', 'close']
    fail_loop = build('camserv', directory / 'camserv-loop-fail', endpoint,
                      log_endpoint, trace, fail_loop=True)
    trace.unlink()
    assert subprocess.run([str(fail_loop), *camera_args(0)], timeout=8).returncode == 3
    assert not trace.exists() and not endpoint.exists()
    failure_messages = [row['message'] for row in existing.records(journal)
                        if row['component'] == 'camserv']
    for event in ('camera open failed', 'camera configuration failed',
                  'camera start failed', 'camera endpoint failed'):
        assert any(event in message for message in failure_messages), event
    print('T-03 camera initialization failures and resource ownership passed')

    trace.unlink(missing_ok=True)
    process = run_service(binary, endpoint, camera_args(6))
    try:
        with connect(endpoint) as connection:
            cam_subscribe(connection)
            connection.settimeout(.8)
            try:
                kind, _ = recv(connection)
                assert kind != 8, 'oversized frame delivered'
            except socket.timeout:
                pass
    finally:
        finish(process)
    assert trace.read_text().count('release\n') >= 1
    trace.unlink()
    process = run_service(binary, endpoint, camera_args(9))
    try:
        with connect(endpoint) as connection:
            connection.sendall(frame(3, b'\x01\x00'))
            assert recv(connection) == (4, b'\0' * 4)
            assert recv(connection)[0] == 7
            kind, payload = recv(connection)
            assert kind == 8 and len(payload) == 1048576
            assert payload[:20] == struct.pack('<QQI', 1, 1001, 1048556)
    finally:
        finish(process)
    print('T-04 camera size boundary and oversized frame passed')

    trace.unlink()
    process = run_service(binary, endpoint, camera_args(5))
    with connect(endpoint) as connection:
        cam_subscribe(connection)
        first = recv(connection)
        second = recv(connection)
        lost = recv(connection)
        assert [first[0], second[0], lost[0]] == [8, 8, 9]
        assert [struct.unpack_from('<Q', item[1])[0] for item in (first, second)] == [1, 2]
        assert lost[1] == struct.pack('<I', 8)
    assert process.wait(timeout=5) == 1 and not endpoint.exists()
    assert trace.read_text().count('release\n') == 2
    assert trace.read_text().count('close\n') == 1
    print('T-06 camera NoFrame, DeviceLost and frame release passed')
    entries = existing.records(journal)
    assert any(row['component'] == 'camserv' and 'started format=Mjpeg width=320' in row['message'] for row in entries)
    assert any(row['component'] == 'camserv' and 'camera device lost' in row['message'] for row in entries)

    trace.unlink()
    process = run_service(binary, endpoint, camera_args(7))
    before = len([row for row in existing.records(journal)
                  if row['component'] == 'camserv' and 'slow or failed client' in row['message']])
    try:
        with connect(endpoint) as slow, connect(endpoint) as fast:
            cam_subscribe(slow, 262144)
            cam_subscribe(fast, 262144)
            for _ in range(80):
                kind, payload = recv(fast)
                assert kind == 8 and len(payload) == 262164
            wait(lambda: len([row for row in existing.records(journal)
                              if row['component'] == 'camserv' and
                              'slow or failed client' in row['message']]) > before,
                 'camera slow client close')
            assert recv(fast)[0] == 8
    finally:
        finish(process)
    assert process.returncode == 0
    print('T-07 camera slow client isolated; fast client continued')


def io_subscribe(connection, mask, expected_devices=1):
    connection.sendall(frame(1, struct.pack('<HI', 1, mask)))
    assert recv(connection) == (2, b'\0' * 4)
    added = [recv(connection) for _ in range(expected_devices)]
    assert all(kind == 6 and len(payload) == 20 for kind, payload in added)
    return added


def test_input(directory, log_endpoint, journal):
    endpoint = directory / 'input.sock'
    trace = directory / 'input.trace'
    binary = build('IOserv', directory / 'IOserv-test', endpoint, log_endpoint, trace)
    assert subprocess.run([str(binary)], timeout=5).returncode == 2
    paths = ['--device=/dev/input/event' + str(i) for i in (0, 1, 2, 3)]
    process = run_service(binary, endpoint, paths)
    try:
        with connect(endpoint) as keyboard, connect(endpoint) as pointer, connect(endpoint) as touch:
            devices = [io_subscribe(keyboard, 1), io_subscribe(pointer, 2), io_subscribe(touch, 4)]
            classes = [struct.unpack_from('<I', group[0][1], 16)[0] for group in devices]
            assert classes == [1, 2, 4]
            keyboard.sendall(frame(1, struct.pack('<HI', 1, 1)))
            assert recv(keyboard) == (2, struct.pack('<I', 3))
            kind, data = recv(keyboard)
            assert kind == 5 and data[18:] == b'\x01\x23\x01\xfd\xff\xff\xff' + b'\x04\0\0\0\0\0\0'
            keyboard.sendall(frame(3))
            assert recv(keyboard) == (4, b'\0' * 4)
            keyboard.sendall(frame(3))
            assert recv(keyboard) == (4, struct.pack('<I', 3))
            keyboard.sendall(frame(99))
            assert recv(keyboard) == (8, struct.pack('<I', 1))
            keyboard.sendall(frame(5))
            assert recv(keyboard) == (8, struct.pack('<I', 1))
            keyboard.sendall(frame(1, b'\x01\0\x01'))
            assert recv(keyboard) == (2, struct.pack('<I', 1))
            keyboard.sendall(frame(1, struct.pack('<HI', 2, 1)))
            assert recv(keyboard) == (2, struct.pack('<I', 12))
            keyboard.sendall(frame(1, struct.pack('<HI', 1, 8)))
            assert recv(keyboard) == (2, struct.pack('<I', 12))
            keyboard.sendall(frame(1, struct.pack('<HI', 1, 5)))
            assert recv(keyboard) == (2, b'\0' * 4)
            added = [recv(keyboard) for _ in range(2)]
            assert {struct.unpack_from('<I', item[1], 16)[0] for item in added} == {1, 4}
            assert recv(pointer)[0] == 5 and recv(touch)[0] == 5
            with connect(endpoint) as malformed:
                malformed.sendall(struct.pack('<II', 1048577, 1))
                assert malformed.recv(1) == b''
            assert recv(pointer)[0] == 5
    finally:
        finish(process)
    assert process.returncode == 0 and not endpoint.exists()
    marks = trace.read_text().splitlines()
    assert marks.count('open') == 4 and marks.count('close') == 3
    assert marks.count('destroy') >= 3
    print('T-08 T-09 T-10 T-13 input multi-device subscriptions and wire bytes passed')

    trace.unlink()
    process = run_service(binary, endpoint, ['--device=/dev/input/event3'])
    try:
        with connect(endpoint) as connection:
            connection.sendall(frame(1, struct.pack('<HI', 1, 1)))
            assert recv(connection) == (2, b'\0' * 4)
            connection.settimeout(.2)
            try: recv(connection); raise AssertionError('unexpected device')
            except socket.timeout: pass
    finally:
        finish(process)
    print('T-08 input Ready with zero open devices passed')

    trace.unlink()
    process = run_service(binary, endpoint, ['--device=/dev/input/event5'])
    try:
        with connect(endpoint) as connection:
            io_subscribe(connection, 1)
            normal, sync, after = recv(connection), recv(connection), recv(connection)
            assert normal[0] == sync[0] == after[0] == 5
            assert sync[1][16:] == struct.pack('<HBHi', 1, 5, 0, 0)
            assert struct.unpack_from('<Q', after[1], 8)[0] == 3
    finally:
        finish(process)
    assert trace.read_text().count('sync_lost\n') == 1
    print('T-10 input SyncLost and subsequent frame passed')

    trace.unlink()
    process = run_service(binary, endpoint,
                          ['--device=/dev/input/event4', '--device=/dev/input/event1'])
    try:
        with connect(endpoint) as keyboard, connect(endpoint) as pointer:
            device = io_subscribe(keyboard, 1)[0][1]
            io_subscribe(pointer, 2)
            assert [recv(keyboard)[0] for _ in range(2)] == [5, 5]
            removed = recv(keyboard)
            assert removed == (7, device)
            assert recv(pointer)[0] == 5
    finally:
        finish(process)
    assert trace.read_text().count('close\n') == 2
    assert trace.read_text().count('open\n') == 2
    print('T-11 one input DeviceLost leaves another device active passed')
    entries = existing.records(journal)
    assert any(row['component'] == 'IOserv' and 'input device open failed' in row['message'] for row in entries)
    assert any(row['component'] == 'IOserv' and 'input synchronization lost' in row['message'] for row in entries)
    assert not any(row['component'] == 'IOserv' and ('291' in row['message'] or '-3' in row['message']) for row in entries)
    print('T-15 input journal contains technical events without raw input passed')

    trace.unlink()
    process = run_service(binary, endpoint, ['--device=/dev/input/event6'])
    before = len([row for row in existing.records(journal)
                  if row['component'] == 'IOserv' and 'slow or failed client' in row['message']])
    try:
        with connect(endpoint) as slow, connect(endpoint) as fast:
            io_subscribe(slow, 1)
            io_subscribe(fast, 1)
            for _ in range(3200):
                kind, payload = recv(fast)
                assert kind == 5 and len(payload) == 18 + 7 * 256
                if len([row for row in existing.records(journal)
                        if row['component'] == 'IOserv' and
                        'slow or failed client' in row['message']]) > before:
                    break
            else:
                raise AssertionError('IOserv did not disconnect slow client')
            assert recv(fast)[0] == 5
            with connect(endpoint) as fresh:
                io_subscribe(fresh, 1)
                assert recv(fresh)[0] == 5
    finally:
        finish(process)
    assert process.returncode == 0
    print('T-12 input slow client isolated and fresh subscription passed')


def test_supervisor(directory, log_binary, log_endpoint):
    io_binary = directory / 'IOserv'
    cam_binary = directory / 'camserv'
    shutil.copy2(directory / 'IOserv-test', io_binary)
    shutil.copy2(directory / 'camserv-test', cam_binary)
    registry = json.loads((HERE / 'platform_registry.json').read_text())
    journal = directory / 'supervised.log'
    for service in registry['services']:
        if service['service_id'] == 'logserv':
            service.update(executable=str(log_binary), arguments=['--log-path=' + str(journal)])
        elif service['service_id'] == 'appserv':
            service['enabled'] = False
        elif service['service_id'] == 'IOserv':
            service.update(enabled=True, executable=str(io_binary),
                           arguments=['--device=/dev/input/event0'])
        elif service['service_id'] == 'camserv':
            service.update(enabled=True, executable=str(cam_binary), arguments=camera_args(5))
    registry_path = directory / 'supervisor-registry.json'
    metadata = directory / 'supervisor-applications.json'
    registry_path.write_text(json.dumps(registry))
    metadata.write_text('{"applications": []}')
    init_binary = directory / 'initserv-test'
    existing.compile_binary('initserv', init_binary,
                            {'INIT_LOG_ENDPOINT': str(log_endpoint)}, registry_path, metadata)
    cam_endpoint = directory / 'camera.sock'
    io_endpoint = directory / 'input.sock'
    process = subprocess.Popen([str(init_binary)])
    try:
        wait(lambda: cam_endpoint.exists() and io_endpoint.exists(), 'supervised endpoints', 10)
        with connect(io_endpoint) as input_client:
            io_subscribe(input_client, 1)
            with connect(cam_endpoint) as camera_client:
                cam_subscribe(camera_client)
                assert [recv(camera_client)[0] for _ in range(3)] == [8, 8, 9]
            assert recv(input_client)[0] == 5
            wait(lambda: any(row['component'] == 'initserv' and
                             'restarted service_id=camserv' in row['message']
                             for row in existing.records(journal)), 'camserv restart', 8)
            wait(cam_endpoint.exists, 'restarted camserv endpoint', 5)
            with connect(cam_endpoint) as restarted:
                cam_subscribe(restarted)
                kind, payload = recv(restarted)
                assert kind == 8 and struct.unpack_from('<Q', payload)[0] == 1
            assert recv(input_client)[0] == 5
        live = existing.child_pids(process)
        finish(process)
        assert process.returncode == 0
        assert not cam_endpoint.exists() and not io_endpoint.exists() and not log_endpoint.exists()
        assert all(not Path(f'/proc/{pid}').exists() for pid in live)
    finally:
        finish(process)
    print('T-14 initserv start, camera restart, SIGTERM and child reaping passed')


def main():
    with tempfile.TemporaryDirectory(prefix='punos-devices-') as folder:
        directory = Path(folder)
        log_endpoint = directory / 'log.sock'
        journal = directory / 'journal.log'
        log_binary = directory / 'logserv-test'
        existing.compile_binary('logserv', log_binary, {'LOG_ENDPOINT': str(log_endpoint)})
        logger = subprocess.Popen([str(log_binary), '--log-path=' + str(journal)])
        try:
            wait(lambda: log_endpoint.exists() or logger.poll() is not None, 'logserv endpoint')
            assert log_endpoint.exists(), ('logserv exited before endpoint', logger.returncode)
            test_camera(directory, log_endpoint, journal)
            test_input(directory, log_endpoint, journal)
        finally:
            finish(logger)
        assert logger.returncode == 0
        test_supervisor(directory, log_binary, log_endpoint)
    print('device services integration passed')


if __name__ == '__main__':
    main()
