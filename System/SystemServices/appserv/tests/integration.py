#!/usr/bin/env python3
"""Real CRT, process, timer, epoll and ipclib; only logserv is a test peer."""
import argparse
import json
import signal
import socket
import struct
import subprocess
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TIMEOUT = 8


def exact(connection, size):
    data = bytearray()
    while len(data) < size:
        part = connection.recv(size - len(data))
        if not part:
            raise EOFError("IPC peer closed")
        data.extend(part)
    return bytes(data)


def frame(message_type, payload=b""):
    return struct.pack("<II", len(payload), message_type) + payload


def receive(connection):
    size, message_type = struct.unpack("<II", exact(connection, 8))
    assert size <= 1048576
    return message_type, exact(connection, size)


def call(connection, message_type, payload=b"", error=0):
    connection.sendall(frame(message_type, payload))
    reply_type, reply = receive(connection)
    assert reply_type == message_type + 1, (reply_type, message_type)
    assert struct.unpack_from("<I", reply)[0] == error, reply
    if error:
        assert len(reply) == 4, "undefined error fields leaked"
    return reply[4:]


def app_id(value, version=1):
    encoded = value.encode("utf-8")
    return struct.pack("<HH", version, len(encoded)) + encoded


def instance_id(value):
    return struct.pack("<HQ", 1, value)


def wait_for(check, description):
    deadline = time.monotonic() + TIMEOUT
    while time.monotonic() < deadline:
        if check():
            return
        time.sleep(0.02)
    raise AssertionError(f"timed out: {description}")


class LogPeer:
    def __init__(self, path):
        self.records = []
        self.errors = []
        self.connections = []
        self.threads = []
        self.reject = False
        self.closed = False
        self.listener = socket.socket(socket.AF_UNIX)
        self.listener.bind(str(path))
        self.listener.listen()
        self.listener.settimeout(0.1)
        self.thread = threading.Thread(target=self.accept, daemon=True)
        self.thread.start()

    def accept(self):
        while not self.closed:
            try:
                connection, _ = self.listener.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            self.connections.append(connection)
            thread = threading.Thread(target=self.serve, args=(connection,), daemon=True)
            self.threads.append(thread)
            thread.start()

    def serve(self, connection):
        try:
            while not self.closed:
                message_type, payload = receive(connection)
                assert message_type == 1
                version, pid, tid, severity, component_size, message_size = struct.unpack_from("<HiiBHI", payload)
                assert version == 1 and pid > 0 and tid > 0 and severity in range(6)
                assert len(payload) == 17 + component_size + message_size
                assert payload[17:17 + component_size] == b"appserv"
                message = payload[17 + component_size:].decode("utf-8")
                assert "--fixed" not in message and "MODE=fixed" not in message
                self.records.append(message)
                connection.sendall(frame(2, struct.pack("<I", 11 if self.reject else 0)))
        except (EOFError, OSError):
            pass
        except BaseException as error:
            self.errors.append(error)
        finally:
            connection.close()

    def close(self):
        self.closed = True
        self.listener.close()
        for connection in self.connections:
            try:
                connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
        self.thread.join(timeout=1)
        for thread in self.threads:
            thread.join(timeout=1)
        assert not self.errors, self.errors


def build_daemons(punos, build, directory, endpoint, log_endpoint, child, marker):
    native_apps = [
        {"application_id": "wait", "display_name": "Ожидание", "executable": str(child),
         "default_arguments": ["wait", str(marker)]},
        {"application_id": "exit", "display_name": "Exit", "executable": str(child),
         "default_arguments": ["exit"]},
        {"application_id": "signal", "display_name": "Signal", "executable": str(child),
         "default_arguments": ["signal"]},
        {"application_id": "missing", "display_name": "Missing", "executable": str(directory / "absent"),
         "default_arguments": []},
    ]
    for app in native_apps:
        app.update(environment=["MODE=fixed"], terminates_on_sigterm=True)
    registry = {"native_apps": [{key: app[key] for key in ("application_id", "executable")}
                                for app in native_apps]}
    (directory / "registry.json").write_text(json.dumps(registry))
    (directory / "apps.json").write_text(json.dumps({"applications": native_apps}))
    subprocess.run(["python3", str(ROOT / "tools/generate_catalog.py"),
                    "--registry", str(directory / "registry.json"),
                    "--metadata", str(directory / "apps.json"), "--output", str(directory)], check=True)
    libraries = ["core", "mem", "str", "process", "time", "thread", "eventloop"]
    includes = [ROOT / "include", ROOT / "src", directory,
                punos / "System/NativeRuntime/SyscallWrappers/include",
                punos / "System/NativeRuntime/include",
                punos / "System/SpecialLibs/ipclib/include"]
    includes += [punos / f"System/SystemLibs/lib{lib}/include" for lib in libraries]
    flags = ["-std=c11", "-ffreestanding", "-nostdinc", "-fno-builtin", "-fno-stack-protector",
             "-fno-pic", "-m64", "-Wall", "-Wextra", "-Werror", "-fno-tree-loop-distribute-patterns"]
    flags += [f"-I{path}" for path in includes]
    flags += [f'-DAPP_ENDPOINT="{endpoint}"', f'-DAPP_LOG_ENDPOINT="{log_endpoint}"']
    archives = [punos / f"System/SystemLibs/lib{lib}/build/libpunos_{lib}.a" for lib in libraries]
    archives += [punos / "System/SpecialLibs/ipclib/build/libpunos_special_ipc.a",
                 punos / "System/NativeRuntime/CRT/build/libpunos_crt.a",
                 punos / "System/NativeRuntime/SyscallWrappers/build/libpunos_syscall.a"]
    common = [build / "catalog.o", build / "service.o"]
    for name in ("main", "logger"):
        obj = directory / f"{name}.o"
        subprocess.run(["cc", *flags, "-c", str(ROOT / f"src/{name}.c"), "-o", str(obj)], check=True)
        common.append(obj)
    def link(source, output):
        obj = output.with_suffix(".o")
        subprocess.run(["cc", *flags, "-c", str(source), "-o", str(obj)], check=True)
        subprocess.run(["cc", "-nostdlib", "-no-pie", "-Wl,-e,_start",
                        str(punos / "System/NativeRuntime/CRT/build/crt0.o"),
                        *map(str, common), str(obj), "-Wl,--start-group", *map(str, archives),
                        "-Wl,--end-group", "-o", str(output)], check=True)
    executable = directory / "appserv"
    link(directory / "catalog.c", executable)
    invalid_source = directory / "invalid_catalog.c"
    invalid_source.write_text((directory / "catalog.c").read_text().replace(
        "{ (char *)PLATFORM_REGISTRY_ID, 64 }",
        '{ (char *)"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", 64 }', 1))
    invalid_executable = directory / "invalid-appserv"
    link(invalid_source, invalid_executable)
    return executable, invalid_executable


def status(connection, instance):
    return struct.unpack("<BBi", call(connection, 9, instance_id(instance)))


def launch(connection, name):
    return struct.unpack("<Q", call(connection, 5, app_id(name)))[0]


def children_of(process):
    path = Path(f"/proc/{process.pid}/task/{process.pid}/children")
    return path.read_text().split() if path.exists() else []


def run_tests(punos, build):
    with tempfile.TemporaryDirectory(prefix="appserv-") as temporary:
        directory = Path(temporary)
        endpoint, log_endpoint, marker = (directory / name for name in ("app.sock", "log.sock", "ready"))
        executable, invalid = build_daemons(punos, build, directory, endpoint, log_endpoint, build / "child", marker)
        assert subprocess.run([str(invalid)], timeout=TIMEOUT).returncode == 3
        assert not endpoint.exists(), "invalid catalogue exposed endpoint"
        assert subprocess.run([str(executable)], timeout=TIMEOUT).returncode == 3
        assert not endpoint.exists(), "missing logserv exposed endpoint"
        assert subprocess.run([str(executable), "--arbitrary-executable"], timeout=TIMEOUT).returncode == 2
        log = LogPeer(log_endpoint)
        process = subprocess.Popen([str(executable)])
        connection = None
        try:
            wait_for(endpoint.exists, "appserv endpoint")
            assert children_of(process) == [], "startup launched applications"
            connection = socket.socket(socket.AF_UNIX)
            connection.settimeout(TIMEOUT)
            connection.connect(str(endpoint))
            listing = call(connection, 1)
            assert struct.unpack_from("<H", listing)[0] == 4
            assert b"MODE=fixed" not in listing and str(build / "child").encode() not in listing
            info = call(connection, 3, app_id("wait"))
            assert "Ожидание".encode() in info
            call(connection, 3, app_id("absent"), error=5)
            call(connection, 5, app_id("wait", version=2), error=12)
            call(connection, 5, app_id("wait") + b"--evil", error=1)
            call(connection, 5, struct.pack("<HH", 1, 1) + b"\xff", error=1)
            call(connection, 7, instance_id(0), error=1)
            call(connection, 9, instance_id(999999), error=5)
            call(connection, 1, b"extra", error=1)
            connection.sendall(frame(99))
            assert receive(connection) == (99, struct.pack("<I", 1))
            # Fragmented frame, then a burst larger than the per-client batch.
            fragmented = frame(3, app_id("wait"))
            for byte in fragmented:
                connection.sendall(bytes([byte]))
            assert receive(connection)[0] == 4
            connection.sendall(frame(1) * 150)
            for _ in range(150):
                message_type, payload = receive(connection)
                assert message_type == 2 and struct.unpack_from("<I", payload)[0] == 0
            exit_instance = launch(connection, "exit")
            assert exit_instance == 1
            wait_for(lambda: status(connection, exit_instance)[0] == 3, "normal exit")
            assert status(connection, exit_instance) == (3, 0, 37)
            wait_for(lambda: children_of(process) == [], "child reaping")
            time.sleep(1.1)
            assert status(connection, exit_instance) == (3, 0, 37)
            assert children_of(process) == [], "exited app restarted"
            call(connection, 7, instance_id(exit_instance), error=3)
            signaled = launch(connection, "signal")
            wait_for(lambda: status(connection, signaled)[0] == 3, "signal exit")
            assert status(connection, signaled) == (3, 1, 15)
            missing = launch(connection, "missing")
            # libprocess reports fork success; exec failure is exit 127.
            wait_for(lambda: status(connection, missing)[0] == 3, "exec failure")
            assert status(connection, missing) == (3, 0, 127)
            running = launch(connection, "wait")
            wait_for(marker.exists, "SIGTERM handler readiness")
            assert status(connection, running)[0] == 1
            call(connection, 7, instance_id(running))
            wait_for(lambda: status(connection, running)[0] == 3, "requested stop")
            assert status(connection, running) == (3, 0, 23)
            # Malformed ipclib header is isolated to the offending client.
            bad = socket.socket(socket.AF_UNIX)
            bad.settimeout(TIMEOUT)
            bad.connect(str(endpoint))
            bad.sendall(struct.pack("<II", 1048577, 1))
            assert bad.recv(1) == b""
            bad.close()
            call(connection, 1)
            # Client departure does not terminate its application.
            marker.unlink()
            orphan_connection = socket.socket(socket.AF_UNIX)
            orphan_connection.settimeout(TIMEOUT)
            orphan_connection.connect(str(endpoint))
            live = launch(orphan_connection, "wait")
            orphan_connection.close()
            wait_for(marker.exists, "second live child")
            assert live > running and status(connection, live)[0] == 1
            live_pids = children_of(process)
            assert live_pids
            # Verify pending outbound responses under a slow reader.
            slow = socket.socket(socket.AF_UNIX)
            slow.settimeout(TIMEOUT)
            slow.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
            slow.connect(str(endpoint))
            slow.sendall(frame(1) * 4000)
            time.sleep(0.1)
            call(connection, 1)  # Other clients remain responsive.
            for _ in range(4000):
                assert receive(slow)[0] == 2
            slow.close()
            process.send_signal(signal.SIGTERM)
            assert process.wait(timeout=TIMEOUT) == 0
            assert not endpoint.exists()
            assert all(not Path(f"/proc/{pid}").exists() for pid in live_pids)
            wait_for(lambda: any("stopped" in message for message in log.records), "shutdown log")
            for event in ("ready", "launched", "stop_requested", "exited", "invalid_request"):
                assert any(event in message for message in log.records), event
            assert not log.errors
            # Refused journal entries fail the service instead of hiding loss.
            log.reject = True
            failed = subprocess.Popen([str(executable)])
            try:
                assert failed.wait(timeout=TIMEOUT) == 1
                assert not endpoint.exists()
            finally:
                if failed.poll() is None:
                    failed.kill()
                    failed.wait()
        finally:
            if connection is not None:
                connection.close()
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
                try:
                    process.wait(timeout=TIMEOUT)
                except subprocess.TimeoutExpired:
                    process.kill()  # Test cleanup only; production never uses SIGKILL.
                    process.wait()
            log.close()
    print("appserv Linux integration tests passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--punos", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    args = parser.parse_args()
    run_tests(args.punos, args.build)
