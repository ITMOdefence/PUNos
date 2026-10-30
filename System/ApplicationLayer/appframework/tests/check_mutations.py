#!/usr/bin/env python3
"""Regression mutations for appframework, applied to a disposable copy of System/.

Each mutation breaks one rule of DevDoc/ApplicationLayer/appframework.md; the
runtime tests must notice (non-zero exit of `make check`, and a test program ran and failed;
a build error does not count). Run on x86_64 Linux with GCC."""
import argparse
from dataclasses import dataclass
from pathlib import Path
import shutil
import subprocess
import tempfile

LIBRARY = "ApplicationLayer/appframework"
LOGGING = LIBRARY + "/src/logging.c"
APPS = LIBRARY + "/src/applications.c"
APPLICATION = LIBRARY + "/src/application.c"
EVENTLOOP = LIBRARY + "/src/eventloop.c"
RUNTIME = LIBRARY + "/src/runtime.c"
MESSAGING = LIBRARY + "/src/messaging.c"


@dataclass(frozen=True)
class Mutation:
    name: str
    source: str
    changes: tuple

    def apply(self, text):
        for old, new in self.changes:
            if text.count(old) != 1:
                raise ValueError(f"{self.name}: expected exactly one source match: {old!r}")
            text = text.replace(old, new, 1)
        return text


MUTATIONS = [
    Mutation("log: pending limit off by one", LOGGING, (
        ("pending_records(client) >= APP_LOG_MAX_PENDING", "pending_records(client) > APP_LOG_MAX_PENDING"),)),
    Mutation("log: ipclib overflow is not WouldBlock", LOGGING, (
        ("if (result == CORE_OVERFLOW) {\n        return CORE_WOULD_BLOCK;",
         "if (false && result == CORE_OVERFLOW) {\n        return CORE_WOULD_BLOCK;"),)),
    Mutation("log: reply accounting lost", LOGGING, (
        ("client->unacknowledged++;", ""),)),
    Mutation("destroy: second destroy from a callback allowed", APPLICATION, (
        ("operation == APP_OPERATION_DESTROY && app->destroying",
         "false && operation == APP_OPERATION_DESTROY && app->destroying"),)),
    Mutation("apps: list over 64 entries accepted", APPS, (
        ("count > APPSERV_LIST_MAX", "false && count > APPSERV_LIST_MAX"),)),
    Mutation("apps: launch with instance 0 accepted", APPS, (
        ("out->result.instance == 0 ? CORE_IO_ERROR : CORE_OK", "CORE_OK"),)),
    Mutation("apps: status Starting accepted", APPS, (
        ("state != APP_INSTANCE_STATE_RUNNING && state != APP_INSTANCE_STATE_TERMINATING &&\n            state != APP_INSTANCE_STATE_EXITED",
         "false"),)),
    Mutation("apps: stop reply with extra bytes accepted", APPS, (
        ("size == ERROR_CODE_SIZE ? CORE_OK : CORE_IO_ERROR", "CORE_OK"),)),
    Mutation("apps: failed batch stays in the queue during callbacks", APPS, (
        ("    client->head = (client->head + taken) % APP_APPS_MAX_PENDING;\n    client->count -= taken;\n    client->ending += taken;\n"
         "    for (usize i = 0; i < taken; ++i) {\n        AppRequest request = client->requests[(first + i) % APP_APPS_MAX_PENDING];\n\n        client->ending--;\n",
         "    (void)first;\n    for (usize i = 0; i < taken; ++i) {\n        AppRequest request = *request_at(client, 0);\n\n        client->head = (client->head + 1) % APP_APPS_MAX_PENDING;\n        client->count--;\n"),)),
    Mutation("apps: slots of a failing batch are not reserved", APPS, (
        ("return client->count + client->ending >= APP_APPS_MAX_PENDING;",
         "return client->count >= APP_APPS_MAX_PENDING;"),)),
    Mutation("apps: failed write ends requests inside the public call", APPS, (
        ("client->failed = true;\n                (void)eventloop_wake(app->loop);",
         "fail_connection(app, CORE_IO_ERROR, \"appserv connection failed\", APP_LOG_LEVEL_WARNING);"),)),
    Mutation("sigterm: destroy does not wait for a running handler", RUNTIME, (
        ("while (__atomic_load_n(&handlers_running, __ATOMIC_SEQ_CST) != 0) {", "while (false) {"),)),
    Mutation("events: internal source removable by the application", EVENTLOOP, (
        ("entry == NULL || entry->internal", "entry == NULL"),)),
    Mutation("messaging: pending limit off by one", MESSAGING, (
        ("return client->count + client->ending >= APP_MSG_MAX_PENDING;",
         "return client->count + client->ending > APP_MSG_MAX_PENDING;"),)),
    Mutation("messaging: ipclib overflow is not WouldBlock", MESSAGING, (
        ("if (result == CORE_OVERFLOW) {\n        return CORE_WOULD_BLOCK;\n    }\n    if (result == CORE_OK) {",
         "if (false && result == CORE_OVERFLOW) {\n        return CORE_WOULD_BLOCK;\n    }\n    if (result == CORE_OK) {"),)),
    Mutation("messaging: request_result with correlation 0 accepted", MESSAGING, (
        ("if (result.correlation == 0) {\n            return false;", "if (false) {\n            return false;"),)),
    Mutation("messaging: respond with correlation 0 accepted", MESSAGING, (
        ("answer.correlation == 0 || error > (u32)LAST_ERROR_CODE", "error > (u32)LAST_ERROR_CODE"),)),
    Mutation("messaging: empty secret accepted", MESSAGING, (
        ("secret == NULL || secret_length == 0 ||", "secret == NULL ||"),)),
    Mutation("messaging: subscriptions not restored after reconnect", MESSAGING, (
        ("        if (!entry->used) {\n            continue;\n        }\n        result = libappframework_internal_msg_encode(APP_MSG_OPERATION_SUBSCRIBE,",
         "        if (!entry->used || true) {\n            continue;\n        }\n        result = libappframework_internal_msg_encode(APP_MSG_OPERATION_SUBSCRIBE,"),)),
    Mutation("messaging: answer nobody asked for accepted", MESSAGING, (
        ("if (client->count == 0 || type != reply_type(request_at(client, 0)->operation)) {",
         "if (client->count != 0 && type != reply_type(request_at(client, 0)->operation)) {"),)),
    Mutation("messaging: permanent refusal retried forever", MESSAGING, (
        ("if (client->retry != 0 || client->permanent || !client->started) {",
         "if (client->retry != 0 || !client->started) {"),)),
]


def check(system):
    result = subprocess.run(["make", "-C", str(system / LIBRARY), "check"],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return result.returncode, result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[4])
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="punos-appfw-mutations-") as directory:
        system = Path(directory) / "System"
        shutil.copytree(args.source / "System", system,
                        ignore=shutil.ignore_patterns("build", "download", "rootfs", "images", "linux-*"))
        status, output = check(system)
        if status:
            raise RuntimeError(f"Baseline failed:\n{output}")
        for mutation in MUTATIONS:
            source = system / mutation.source
            original = source.read_text(encoding="utf-8")
            try:
                source.write_text(mutation.apply(original), encoding="utf-8")
                status, output = check(system)
                broke_build = " error: " in output or "undefined reference" in output
                if status == 0 or broke_build:
                    raise RuntimeError(f"{mutation.name}: survived or broke the build:\n{output[-3000:]}")
                failed = [l for l in output.splitlines()
                          if "runtime test failed" in l or "Segmentation" in l or "Aborted" in l]
                print(f"{mutation.name}: detected ({failed[0].strip() if failed else 'test exit'})",
                      flush=True)
            finally:
                source.write_text(original, encoding="utf-8")
        status, output = check(system)
        if status:
            raise RuntimeError(f"Restored tree failed:\n{output}")
    print(f"All {len(MUTATIONS)} mutations detected.")


if __name__ == "__main__":
    main()
