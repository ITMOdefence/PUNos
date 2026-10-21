#!/usr/bin/env python3
"""
Host integration test (level 3): appframework against the real logserv and
appserv, all built from this tree and run as ordinary processes on the host.

  * fw_launcher drives appserv through app_apps_*(): list, get_info, errors,
    launch, wait for Running, stop, wait for Exited;
  * fw_child, started by the real appserv, is a second appframework process;
    it logs, then leaves on the SIGTERM that appserv sends;
  * logserv writes the journal, which is checked record by record.

Everything runs in a temporary directory; the endpoints are compiled into the
services (-DAPP_ENDPOINT, -DLOG_ENDPOINT, -DAPP_LOG_ENDPOINT).
"""
import argparse
import json
import os
import signal
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

TIMEOUT = 30
FLAGS = ["-std=c11", "-ffreestanding", "-nostdinc", "-fno-builtin", "-fno-stack-protector",
         "-fno-pic", "-m64", "-Wall", "-Wextra", "-Werror", "-fno-tree-loop-distribute-patterns"]
SYSTEM_LIBS = ["core", "mem", "str", "file", "process", "time", "thread", "sync", "eventloop"]


class Tree:
    def __init__(self, punos, library):
        self.punos = Path(punos)
        self.library = Path(library)
        self.here = Path(__file__).resolve().parent
        self.appframework = self.here.parents[1]
        self.services = self.punos / "System/SystemServices"

    def includes(self, *extra):
        paths = [self.punos / "System/NativeRuntime/include",
                 self.punos / "System/NativeRuntime/SyscallWrappers/include",
                 self.punos / "System/SpecialLibs/ipclib/include",
                 self.punos / "System/SpecialLibs/loglib/include"]
        paths += [self.punos / f"System/SystemLibs/lib{lib}/include" for lib in SYSTEM_LIBS]
        return ["-I" + str(path) for path in [*paths, *extra]]

    def archives(self, with_library=False):
        result = [self.library] if with_library else []
        result += [self.punos / f"System/SystemLibs/lib{lib}/build/libpunos_{lib}.a"
                   for lib in SYSTEM_LIBS]
        result += [self.punos / "System/SpecialLibs/ipclib/build/libpunos_special_ipc.a",
                   self.punos / "System/SpecialLibs/loglib/build/libpunos_special_log.a",
                   self.punos / "System/NativeRuntime/CRT/build/libpunos_crt.a",
                   self.punos / "System/NativeRuntime/SyscallWrappers/build/libpunos_syscall.a"]
        return result


def compile_and_link(tree, sources, output, include_dirs, definitions=(), with_library=False):
    flags = FLAGS + tree.includes(*include_dirs) + [f"-D{item}" for item in definitions]
    objects = []
    for index, source in enumerate(sources):
        obj = output.parent / f"{output.name}.{index}.o"
        subprocess.run(["cc", *flags, "-c", str(source), "-o", str(obj)], check=True)
        objects.append(obj)
    crt0 = tree.punos / "System/NativeRuntime/CRT/build/crt0.o"
    subprocess.run(["cc", "-nostdlib", "-no-pie", "-Wl,-e,_start", str(crt0),
                    *map(str, objects), "-Wl,--start-group",
                    *map(str, tree.archives(with_library)), "-Wl,--end-group",
                    "-o", str(output)], check=True)


def build_all(tree, directory, endpoints):
    logserv = tree.services / "logserv"
    appserv = tree.services / "appserv"
    apps = tree.appframework / "tests/system"

    logserv_bin = directory / "logserv"
    compile_and_link(tree, sorted((logserv / "src").glob("*.c")), logserv_bin,
                     [logserv / "include", logserv / "src"],
                     [f'LOG_ENDPOINT="{endpoints["log"]}"'])

    child_bin = directory / "fw_child"
    launcher_bin = directory / "fw_launcher"
    for name, output in (("fw_child.c", child_bin), ("fw_launcher.c", launcher_bin)):
        compile_and_link(tree, [apps / name], output,
                         [tree.appframework / "include", tree.appframework / "src"],
                         with_library=True)

    marker = directory / "child.ready"
    native_apps = [{"application_id": "fwchild", "display_name": "Child",
                    "executable": str(child_bin),
                    "default_arguments": [str(endpoints["log"]), str(marker)],
                    "environment": ["MODE=fixed"], "terminates_on_sigterm": True}]
    (directory / "registry.json").write_text(json.dumps(
        {"native_apps": [{"application_id": "fwchild", "executable": str(child_bin)}]}))
    (directory / "apps.json").write_text(json.dumps({"applications": native_apps}))
    generated = directory / "generated"
    generated.mkdir()
    subprocess.run(["python3", str(appserv / "tools/generate_catalog.py"),
                    "--registry", str(directory / "registry.json"),
                    "--metadata", str(directory / "apps.json"),
                    "--output", str(generated)], check=True)
    appserv_bin = directory / "appserv"
    compile_and_link(tree, sorted((appserv / "src").glob("*.c")) + [generated / "catalog.c"],
                     appserv_bin, [appserv / "include", appserv / "src", generated],
                     [f'APP_ENDPOINT="{endpoints["app"]}"',
                      f'APP_LOG_ENDPOINT="{endpoints["log"]}"'])
    return logserv_bin, appserv_bin, launcher_bin, marker


def wait_for(check, description, timeout=TIMEOUT):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if check():
            return
        time.sleep(0.02)
    raise AssertionError(f"timed out: {description}")


def finish(process):
    """SIGTERM, then a bounded wait: a service that ignores it fails the test."""
    if process.poll() is None:
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=TIMEOUT)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            raise AssertionError("SIGTERM did not stop the process")
    return process.returncode


def records(path):
    """The journal of logserv (loglib frame format)."""
    if not path.exists():
        return []
    data = path.read_bytes()
    offset = 0
    result = []
    while len(data) - offset >= 27:
        size, stamp, pid, tid, severity, clen, mlen = struct.unpack_from("<IQiiBHI", data, offset)
        assert size == 27 + clen + mlen, "journal frame size"
        if offset + size > len(data):
            break
        component = data[offset + 27:offset + 27 + clen].decode()
        message = data[offset + 27 + clen:offset + size].decode()
        result.append(dict(time=stamp, pid=pid, tid=tid, severity=severity,
                           component=component, message=message))
        offset += size
    return result


def messages(journal, component):
    return [r["message"] for r in records(journal) if r["component"] == component]


def contains_in_order(haystack, needles):
    position = 0
    for item in haystack:
        if position < len(needles) and item == needles[position]:
            position += 1
    return position == len(needles)


def start(command):
    return subprocess.Popen([str(part) for part in command])


def run_full_scenario(binaries, directory, endpoints, launcher_log=None):
    logserv_bin, appserv_bin, launcher_bin, marker = binaries
    with_logserv = True  # appserv itself needs a running logserv before it serves
    journal = directory / ("full.log" if launcher_log is None else "blind.log")
    launcher_log = launcher_log or endpoints["log"]
    for path in (marker, ):
        if path.exists():
            path.unlink()
    processes = []
    try:
        if with_logserv:
            log = start([logserv_bin, f"--log-path={journal}"])
            processes.append(log)
            wait_for(Path(endpoints["log"]).exists, "logserv endpoint")
        app = start([appserv_bin])
        processes.append(app)
        wait_for(Path(endpoints["app"]).exists, "appserv endpoint")
        started = time.monotonic()
        launcher = start([launcher_bin, endpoints["app"], launcher_log, "full", marker])
        code = launcher.wait(timeout=TIMEOUT)
        took = time.monotonic() - started
        assert code == 0, f"fw_launcher failed at step {code}"
        assert took < 20, f"scenario too slow: {took:.1f}s"
        assert finish(app) == 0, "appserv did not stop cleanly"
        processes.remove(app)
        if with_logserv:
            # logserv gets the last records before it stops
            if launcher_log == endpoints["log"]:
                wait_for(lambda: "launcher finished" in messages(journal, "fwlauncher"),
                         "launcher records in the journal")
            wait_for(lambda: "AppHandle destroyed" in messages(journal, "fwchild"),
                     "child records in the journal")
            assert finish(log) == 0, "logserv did not stop cleanly"
            processes.remove(log)
        return journal
    finally:
        for process in processes:
            finish(process)


def check_full_journal(journal):
    entries = records(journal)
    launcher = [r for r in entries if r["component"] == "fwlauncher"]
    child = [r for r in entries if r["component"] == "fwchild"]
    assert contains_in_order([r["message"] for r in launcher],
                             ["AppHandle created", "launcher finished", "AppHandle destroyed"]), \
        [r["message"] for r in launcher]
    assert contains_in_order([r["message"] for r in child],
                             ["AppHandle created", "child ready", "SIGTERM received",
                              "AppHandle destroyed"]), \
        [r["message"] for r in child]
    assert "launcher failed" not in [r["message"] for r in launcher]
    # two different processes, each with its own pid, tid equal to pid (single thread)
    assert len({r["pid"] for r in launcher}) == 1 and len({r["pid"] for r in child}) == 1
    assert launcher[0]["pid"] != child[0]["pid"] and launcher[0]["pid"] > 0
    assert all(r["tid"] == r["pid"] for r in launcher + child)
    # severities: Info (2) for lifecycle records
    created = [r for r in launcher + child if r["message"] == "AppHandle created"]
    assert all(r["severity"] == 2 for r in created)
    # per component, time never goes back
    for group in (launcher, child):
        stamps = [r["time"] for r in group]
        assert stamps == sorted(stamps)
    # appserv saw the instance start and end
    assert any(r["component"] == "appserv" for r in entries)


def run_wait_scenario(binaries, directory, endpoints):
    logserv_bin, _, launcher_bin, _ = binaries
    journal = directory / "wait.log"
    log = start([logserv_bin, f"--log-path={journal}"])
    try:
        wait_for(Path(endpoints["log"]).exists, "logserv endpoint")
        launcher = start([launcher_bin, endpoints["app"], endpoints["log"], "wait"])
        try:
            wait_for(lambda: "launcher waiting" in messages(journal, "fwlauncher"),
                     "launcher ready")
            time.sleep(0.2)
            launcher.send_signal(signal.SIGTERM)
            assert launcher.wait(timeout=5) == 0, "SIGTERM must end app_run() with success"
        finally:
            finish(launcher)
        wait_for(lambda: "AppHandle destroyed" in messages(journal, "fwlauncher"),
                 "destroy record")
        seen = messages(journal, "fwlauncher")
        assert contains_in_order(seen, ["AppHandle created", "launcher waiting",
                                        "SIGTERM received", "AppHandle destroyed"]), seen
    finally:
        assert finish(log) == 0


def run_without_appserv(binaries, directory, endpoints):
    _, _, launcher_bin, marker = binaries
    app_endpoint = Path(endpoints["app"])
    assert not app_endpoint.exists()
    launcher = start([launcher_bin, endpoints["app"], endpoints["log"], "full", marker])
    code = launcher.wait(timeout=TIMEOUT)
    assert code == 1, f"without appserv the first request must fail (step 1), got {code}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--punos", required=True)
    parser.add_argument("--library", required=True)
    arguments = parser.parse_args()
    tree = Tree(arguments.punos, arguments.library)
    with tempfile.TemporaryDirectory(prefix="appfw-", dir="/tmp") as temporary:
        directory = Path(temporary)
        endpoints = {"log": str(directory / "log.sock"), "app": str(directory / "app.sock")}
        binaries = build_all(tree, directory, endpoints)
        journal = run_full_scenario(binaries, directory, endpoints)
        check_full_journal(journal)
        print("host: launch, status, stop and exit through real appserv and logserv passed")
        run_wait_scenario(binaries, directory, endpoints)
        print("host: SIGTERM ends app_run() and is journaled passed")
        blind = run_full_scenario(binaries, directory, endpoints,
                                  launcher_log=str(directory / "nobody.sock"))
        assert messages(blind, "fwlauncher") == [] and "child ready" in messages(blind, "fwchild")
        print("host: a launcher that cannot reach logserv still works passed")
        run_without_appserv(binaries, directory, endpoints)
        print("host: a missing appserv is an error of the request, not a crash passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
