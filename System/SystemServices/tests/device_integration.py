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


def build(name, output, endpoint, log_endpoint, trace, fail_loop=False,
          inject_mask=None, fail_device_registration=False, fail_ipc_allocation=False,
          burst_no_frame=False):
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
    if inject_mask is not None:
        options.append(f'-DINJECTED_EVENT_MASK={inject_mask}')
    if burst_no_frame:
        options.append('-DFAKE_BURST_END_NO_FRAME')
    sources = [module / 'src/main.c', module / 'src/protocol.c',
               HERE / 'common/logger.c',
               module / f'tests/fake_{"camera" if name == "camserv" else "input"}.c']
    if name == 'IOserv':
        sources.append(module / 'src/state.c')
    if fail_loop:
        sources.append(HERE / 'tests/fail_eventloop.c')
    if fail_device_registration:
        sources.append(HERE / 'tests/fail_device_registration.c')
    if fail_ipc_allocation:
        sources.append(HERE / 'tests/fail_ipc_allocation.c')
    if inject_mask is not None:
        sources.append(HERE / 'tests/inject_device_event.c')
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
    if fail_device_registration:
        command.append('-Wl,--wrap=eventloop_add_fd')
    if fail_ipc_allocation:
        command.append('-Wl,--wrap=memory_allocate')
    if inject_mask is not None:
        command += ['-Wl,--wrap=eventloop_add_fd', '-Wl,--wrap=eventloop_wait',
                    '-Wl,--wrap=eventloop_remove_source']
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


def fd_count(process):
    return len(list(Path(f'/proc/{process.pid}/fd').iterdir()))


def wait_eof(connection, label, limit=32 * 1024 * 1024):
    connection.settimeout(5)
    total = 0
    while total <= limit:
        part = connection.recv(min(65536, limit + 1 - total))
        if not part:
            return
        total += len(part)
    raise AssertionError(f'{label}: no EOF after {total} buffered bytes')


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
    wrong_path_args = camera_args(0)
    wrong_path_args[0] = '--device=/dev/Video0'
    assert subprocess.run([str(binary), *wrong_path_args], timeout=8).returncode == 3
    assert not endpoint.exists()
    process = run_service(binary, endpoint, camera_args(0))
    idle_fds = fd_count(process)
    wait(lambda: Path(str(trace) + '.path').exists(), 'camera open path trace')
    assert Path(str(trace) + '.path').read_text() == '/dev/video0'
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
                before = len([row for row in existing.records(journal)
                              if row['component'] == 'camserv' and
                              'IPC receive failed' in row['message']])
                malformed.sendall(struct.pack('<II', 1048577, 3))
                assert malformed.recv(1) == b''
                wait(lambda: len([row for row in existing.records(journal)
                                  if row['component'] == 'camserv' and
                                  'IPC receive failed' in row['message']]) > before,
                     'camera malformed IPC journal')
            with connect(endpoint) as truncated:
                truncated.sendall(struct.pack('<II', 2, 3) + b'\x01')
                truncated.shutdown(socket.SHUT_WR)
                assert truncated.recv(1) == b''
            assert recv(second)[0] == 8
        wait(lambda: fd_count(process) == idle_fds, 'camera disconnected subscriptions')
        with connect(endpoint) as fresh:
            fresh.settimeout(.2)
            try:
                recv(fresh)
                raise AssertionError('new camera connection inherited subscription')
            except socket.timeout:
                pass
            cam_subscribe(fresh)
            assert recv(fresh)[0] == 8
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
    fail_registration = build('camserv', directory / 'camserv-registration-fail', endpoint,
                              log_endpoint, trace, fail_device_registration=True)
    before = len([row for row in existing.records(journal)
                  if row['component'] == 'camserv' and
                  row['message'] == 'camera endpoint failed error=8'])
    assert subprocess.run([str(fail_registration), *camera_args(0)], timeout=8).returncode == 3
    assert trace.read_text().splitlines() == ['open', 'configure', 'start', 'stop', 'close']
    assert not endpoint.exists()
    wait(lambda: len([row for row in existing.records(journal)
                      if row['component'] == 'camserv' and
                      row['message'] == 'camera endpoint failed error=8']) > before,
         'camera late registration failure journal')
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
    for mask, label in ((16, 'HANGUP'), (8, 'ERROR')):
        trace.unlink()
        injected = build('camserv', directory / f'camserv-{label.lower()}', endpoint,
                         log_endpoint, trace, inject_mask=mask)
        process = run_service(injected, endpoint, camera_args(5))
        with connect(endpoint) as connection:
            cam_subscribe(connection)
            assert [recv(connection)[0] for _ in range(3)] == [8, 8, 9]
        assert process.wait(timeout=5) == 1
        marks = trace.read_text().splitlines()
        assert marks.count('device_event_injected') == 1, (label, marks)
        assert marks.count('device_source_removed') == 1, (label, marks)
    print('T-06 camera HANGUP and ERROR device dispatch passed')
    for mask, label in ((16, 'HANGUP'), (8, 'ERROR')):
        trace.unlink()
        injected = build('camserv', directory / f'camserv-burst-{label.lower()}',
                         endpoint, log_endpoint, trace, inject_mask=mask)
        process = run_service(injected, endpoint, camera_args(8))
        with connect(endpoint) as connection:
            cam_subscribe(connection)
            for sequence in range(1, 81):
                kind, payload = recv(connection)
                assert kind == 8 and struct.unpack_from('<Q', payload)[0] == sequence
            assert recv(connection) == (9, struct.pack('<I', 8))
        assert process.wait(timeout=5) == 1
        marks = trace.read_text().splitlines()
        assert marks.count('release') == 80
        assert marks.count('device_event_injected') == 1
        assert marks.count('device_source_removed') == 1
    print('T-06 camera 80 queued frames and DeviceLost under HANGUP/ERROR passed')
    trace.unlink()
    no_frame = build('camserv', directory / 'camserv-burst-no-frame', endpoint,
                     log_endpoint, trace, burst_no_frame=True)
    process = run_service(no_frame, endpoint, camera_args(8))
    try:
        with connect(endpoint) as connection:
            cam_subscribe(connection)
            for sequence in range(1, 81):
                kind, payload = recv(connection)
                assert kind == 8 and struct.unpack_from('<Q', payload)[0] == sequence
            connection.settimeout(.2)
            try:
                recv(connection)
                raise AssertionError('camera frame after NoFrame')
            except socket.timeout:
                pass
            assert process.poll() is None
    finally:
        finish(process)
    assert trace.read_text().count('release\n') == 80
    print('T-06 camera 80 queued frames before NoFrame passed')
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
            wait_eof(slow, 'camera slow client')
    finally:
        finish(process)
    assert process.returncode == 0
    print('T-07 camera slow client isolated; fast client continued')

    alloc_binary = build('camserv', directory / 'camserv-alloc-fail', endpoint,
                         log_endpoint, trace, fail_ipc_allocation=True)
    process = run_service(alloc_binary, endpoint, camera_args(0))
    baseline = fd_count(process)
    before = len([row for row in existing.records(journal)
                  if row['component'] == 'camserv' and
                  row['message'] == 'IPC receive failed error=4'])
    try:
        with connect(endpoint) as failed:
            failed.sendall(frame(1, b'abcdef'))
            wait_eof(failed, 'camera allocation failure')
        wait(lambda: fd_count(process) == baseline, 'camera allocation cleanup')
        with connect(endpoint) as good:
            good.sendall(frame(1))
            assert recv(good)[0] == 2
    finally:
        finish(process)
    assert process.returncode == 0
    wait(lambda: len([row for row in existing.records(journal)
                      if row['component'] == 'camserv' and
                      row['message'] == 'IPC receive failed error=4']) > before,
         'camera IPC allocation error journal')
    print('T-13 camera IPC allocation failure releases client resources')


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
    wait(lambda: trace.exists() and trace.read_text().count('open\n') == 4,
         'initial input device opens')
    idle_fds = fd_count(process)
    try:
        with connect(endpoint) as wire_client:
            wire_client.sendall(bytes.fromhex('06 00 00 00 01 00 00 00 01 00 05 00 00 00'))
            assert exact(wire_client, 12) == bytes.fromhex(
                '04 00 00 00 02 00 00 00 00 00 00 00')
            assert [recv(wire_client)[0] for _ in range(2)] == [6, 6]
        with connect(endpoint) as keyboard, connect(endpoint) as pointer, connect(endpoint) as touch:
            devices = [io_subscribe(keyboard, 1), io_subscribe(pointer, 2), io_subscribe(touch, 4)]
            classes = [struct.unpack_from('<I', group[0][1], 16)[0] for group in devices]
            assert classes == [1, 2, 4]
            ids = [struct.unpack_from('<Q', group[0][1])[0] for group in devices]
            keyboard.sendall(frame(1, struct.pack('<HI', 1, 1)))
            assert recv(keyboard) == (2, struct.pack('<I', 3))
            kind, data = recv(keyboard)
            assert kind == 5 and data[18:] == b'\x01\x23\x01\xfd\xff\xff\xff' + b'\x04\0\0\0\0\0\0'
            keyboard.sendall(frame(3))
            assert recv(keyboard) == (4, b'\0' * 4)
            keyboard.sendall(frame(3))
            assert recv(keyboard) == (4, struct.pack('<I', 3))
            keyboard.sendall(frame(1, struct.pack('<HI', 1, 0)))
            assert recv(keyboard) == (2, struct.pack('<I', 1))
            keyboard.sendall(frame(3, b'\0'))
            assert recv(keyboard) == (4, struct.pack('<I', 1))
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
            assert recv(pointer)[0] == 5
            kind, touch_payload = recv(touch)
            assert kind == 5
            assert touch_payload[18:] == (struct.pack('<BHi', 3, 0x2f, 32) +
                                          struct.pack('<BHi', 3, 0x39, 42) +
                                          struct.pack('<BHi', 3, 0x35, 1234567) +
                                          struct.pack('<BHi', 3, 0x36, -765432) +
                                          struct.pack('<BHi', 6, 0x7abc, 135791113) +
                                          struct.pack('<BHi', 4, 0, 0))
            with connect(endpoint) as malformed:
                before = len([row for row in existing.records(journal)
                              if row['component'] == 'IOserv' and
                              'IPC receive failed' in row['message']])
                malformed.sendall(struct.pack('<II', 1048577, 1))
                assert malformed.recv(1) == b''
                wait(lambda: len([row for row in existing.records(journal)
                                  if row['component'] == 'IOserv' and
                                  'IPC receive failed' in row['message']]) > before,
                     'input malformed IPC journal')
            with connect(endpoint) as truncated:
                truncated.sendall(struct.pack('<II', 6, 1) + b'\x01\0\x01')
                truncated.shutdown(socket.SHUT_WR)
                assert truncated.recv(1) == b''
            assert recv(pointer)[0] == 5
        for mask in range(1, 8):
            expected = {device_id for bit, device_id in ((1, ids[0]),
                                                        (2, ids[1]),
                                                        (4, ids[2])) if mask & bit}
            with connect(endpoint) as filtered:
                added = io_subscribe(filtered, mask, len(expected))
                assert {struct.unpack_from('<Q', item[1])[0] for item in added} == expected
                delivered = set()
                for _ in range(12):
                    kind, payload = recv(filtered)
                    assert kind == 5
                    device_id = struct.unpack_from('<Q', payload)[0]
                    assert device_id in expected, (mask, device_id)
                    delivered.add(device_id)
                assert delivered == expected, (mask, delivered)
        wait(lambda: fd_count(process) == idle_fds, 'input disconnected subscriptions')
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
                          ['--device=/dev/input/event5', '--device=/dev/input/event1'])
    try:
        with connect(endpoint) as connection:
            added = io_subscribe(connection, 3, 2)
            by_class = {struct.unpack_from('<I', item[1], 16)[0]:
                        struct.unpack_from('<Q', item[1])[0] for item in added}
            keyboard_id, pointer_id = by_class[1], by_class[2]
            keyboard_frames, pointer_frames = [], []
            for _ in range(24):
                kind, payload = recv(connection)
                assert kind == 5
                device_id, sequence = struct.unpack_from('<QQ', payload)
                if device_id == keyboard_id:
                    keyboard_frames.append((sequence, payload[18:]))
                else:
                    assert device_id == pointer_id
                    pointer_frames.append(sequence)
                if (len(keyboard_frames) >= 3 and pointer_frames and
                        pointer_frames[-1] >= 3):
                    break
            assert [sequence for sequence, _ in keyboard_frames[:3]] == [1, 2, 3]
            assert keyboard_frames[1][1] == struct.pack('<BHi', 5, 0, 0)
            assert pointer_frames[0] == 1 and pointer_frames[-1] >= 3
    finally:
        finish(process)
    print('T-10 SyncLost emits no synthetic frame and another device continues')

    trace.unlink()
    process = run_service(binary, endpoint,
                          ['--device=/dev/input/event4', '--device=/dev/input/event1'])
    try:
        with connect(endpoint) as keyboard, connect(endpoint) as pointer:
            device = io_subscribe(keyboard, 1)[0][1]
            io_subscribe(pointer, 2)
            before_loss_fds = fd_count(process)
            assert [recv(keyboard)[0] for _ in range(2)] == [5, 5]
            removed = recv(keyboard)
            assert removed == (7, device)
            wait(lambda: trace.read_text().count('close\n') == 1,
                 'lost input handle closed before service shutdown')
            assert fd_count(process) == before_loss_fds - 1
            assert recv(pointer)[0] == 5
    finally:
        finish(process)
    assert trace.read_text().count('close\n') == 2
    assert trace.read_text().count('open\n') == 2
    print('T-11 one input DeviceLost leaves another device active passed')
    for mask, label in ((16, 'HANGUP'), (8, 'ERROR')):
        trace.unlink()
        injected = build('IOserv', directory / f'IOserv-{label.lower()}', endpoint,
                         log_endpoint, trace, inject_mask=mask)
        process = run_service(injected, endpoint,
                              ['--device=/dev/input/event4', '--device=/dev/input/event1'])
        try:
            with connect(endpoint) as keyboard, connect(endpoint) as pointer:
                device = io_subscribe(keyboard, 1)[0][1]
                io_subscribe(pointer, 2)
                assert [recv(keyboard)[0] for _ in range(3)] == [5, 5, 7]
                assert trace.read_text().count('device_event_injected\n') == 1
                wait(lambda: trace.read_text().count('device_source_removed\n') == 1,
                     f'input {label} source removal')
                assert recv(pointer)[0] == 5 and process.poll() is None
        finally:
            finish(process)
    print('T-11 input HANGUP and ERROR device dispatch passed')
    for mask, label in ((16, 'HANGUP'), (8, 'ERROR')):
        trace.unlink()
        injected = build('IOserv', directory / f'IOserv-burst-{label.lower()}',
                         endpoint, log_endpoint, trace, inject_mask=mask)
        process = run_service(injected, endpoint,
                              ['--device=/dev/input/event7', '--device=/dev/input/event1'])
        try:
            with connect(endpoint) as keyboard, connect(endpoint) as pointer:
                device = io_subscribe(keyboard, 1)[0][1]
                io_subscribe(pointer, 2)
                for sequence in range(1, 81):
                    kind, payload = recv(keyboard)
                    assert kind == 5 and struct.unpack_from('<Q', payload, 8)[0] == sequence
                assert recv(keyboard) == (7, device)
                assert recv(pointer)[0] == 5 and process.poll() is None
                wait(lambda: trace.read_text().count('device_source_removed\n') == 1,
                     f'input burst {label} source removal')
        finally:
            finish(process)
        marks = trace.read_text().splitlines()
        assert marks.count('destroy') >= 80
        assert marks.count('open') == marks.count('close') == 2
        assert marks.count('device_event_injected') == 1
    print('T-11 input 80 buffered frames and DeviceLost under HANGUP/ERROR passed')
    trace.unlink()
    no_frame = build('IOserv', directory / 'IOserv-burst-no-frame', endpoint,
                     log_endpoint, trace, burst_no_frame=True)
    process = run_service(no_frame, endpoint, ['--device=/dev/input/event7'])
    try:
        with connect(endpoint) as connection:
            io_subscribe(connection, 1)
            for sequence in range(1, 81):
                kind, payload = recv(connection)
                assert kind == 5 and struct.unpack_from('<Q', payload, 8)[0] == sequence
            connection.settimeout(.2)
            try:
                recv(connection)
                raise AssertionError('input frame after NoFrame')
            except socket.timeout:
                pass
            assert process.poll() is None
    finally:
        finish(process)
    assert trace.read_text().count('destroy\n') == 80
    print('T-11 input 80 buffered frames before NoFrame passed')
    entries = existing.records(journal)
    assert any(row['component'] == 'IOserv' and 'input device open failed' in row['message'] for row in entries)
    assert any(row['component'] == 'IOserv' and 'input synchronization lost' in row['message'] for row in entries)
    private_values = ('291', '-3', '1234567', '-765432', '135791113', '31420')
    assert not any(row['component'] == 'IOserv' and
                   any(value in row['message'] for value in private_values)
                   for row in entries)
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
            wait_eof(slow, 'input slow client')
            with connect(endpoint) as fresh:
                io_subscribe(fresh, 1)
                assert recv(fresh)[0] == 5
    finally:
        finish(process)
    assert process.returncode == 0
    print('T-12 input slow client isolated and fresh subscription passed')

    alloc_binary = build('IOserv', directory / 'IOserv-alloc-fail', endpoint,
                         log_endpoint, trace, fail_ipc_allocation=True)
    process = run_service(alloc_binary, endpoint, ['--device=/dev/input/event0'])
    baseline = fd_count(process)
    before = len([row for row in existing.records(journal)
                  if row['component'] == 'IOserv' and
                  row['message'] == 'IPC receive failed error=4'])
    try:
        with connect(endpoint) as failed:
            failed.sendall(frame(1, struct.pack('<HI', 1, 1)))
            wait_eof(failed, 'input allocation failure')
        wait(lambda: fd_count(process) == baseline, 'input allocation cleanup')
        with connect(endpoint) as good:
            io_subscribe(good, 1)
            assert recv(good)[0] == 5
    finally:
        finish(process)
    assert process.returncode == 0
    wait(lambda: len([row for row in existing.records(journal)
                      if row['component'] == 'IOserv' and
                      row['message'] == 'IPC receive failed error=4']) > before,
         'input IPC allocation error journal')
    print('T-13 input IPC allocation failure releases client resources')


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
                restarted.settimeout(.2)
                try:
                    recv(restarted)
                    raise AssertionError('restarted camera restored an old subscription')
                except socket.timeout:
                    pass
                restarted.settimeout(5)
                cam_subscribe(restarted)
                kind, payload = recv(restarted)
                assert kind == 8 and struct.unpack_from('<Q', payload)[0] == 1
            assert recv(input_client)[0] == 5
            old_io = existing.find_child(process, io_binary)
            assert old_io is not None
            os.kill(old_io, signal.SIGTERM)
            wait_eof(input_client, 'supervised input client')
            wait(lambda: existing.find_child(process, io_binary) not in (None, old_io),
                 'IOserv restart', 10)
            assert not Path(f'/proc/{old_io}').exists()
            wait(lambda: any(row['component'] == 'initserv' and
                             'restarted service_id=IOserv' in row['message']
                             for row in existing.records(journal)), 'IOserv restart log', 8)
            wait(io_endpoint.exists, 'restarted IOserv endpoint', 5)
            with connect(io_endpoint) as new_input:
                new_input.settimeout(.2)
                try:
                    recv(new_input)
                    raise AssertionError('restarted IOserv restored an old subscription')
                except socket.timeout:
                    pass
                new_input.settimeout(5)
                io_subscribe(new_input, 1)
                kind, payload = recv(new_input)
                assert kind == 5 and struct.unpack_from('<Q', payload, 8)[0] == 1
        live = existing.child_pids(process)
        finish(process)
        assert process.returncode == 0
        assert not cam_endpoint.exists() and not io_endpoint.exists() and not log_endpoint.exists()
        assert all(not Path(f'/proc/{pid}').exists() for pid in live)
    finally:
        finish(process)
    print('T-14 initserv start, camera and IOserv restart, SIGTERM and child reaping passed')


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
