#!/usr/bin/env python3
"""
End-to-end messaging test (level 4): several appframework applications talk
through the real msgserv, built from the msgserv snapshot in this tree and run
as ordinary processes on the host.

  * publish reaches two subscribers (sub1, sub2), and both see the event;
  * a response is delivered from a timer, after the request handler returned,
    and an event published between the request and its response does not
    become the response (the requester gets one event and one response);
  * an application started without PUNOS_MSG_SECRET gets CORE_UNSUPPORTED;
  * subscriptions survive a restart of msgserv: after the broker comes back,
    the subscribers receive the next event without subscribing again;
  * app_destroy() with a request that msgserv has not accepted calls its
    callback exactly once, with CORE_INVALID_STATE.

The secrets, permissions and the permissions catalogue come from
tools/generate_messaging.py, so the broker sees the same rules as in production.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from host_integration import Tree, compile_and_link, finish, wait_for, TIMEOUT

APPLICATIONS = {
    "pub": {"publish_topics": ["news"], "subscribe_topics": [],
            "serve_methods": [], "call_targets": []},
    "sub1": {"publish_topics": [], "subscribe_topics": ["news"],
             "serve_methods": [], "call_targets": []},
    "sub2": {"publish_topics": [], "subscribe_topics": ["news"],
             "serve_methods": [], "call_targets": []},
    "srv": {"publish_topics": ["news"], "subscribe_topics": [],
            "serve_methods": ["echo"], "call_targets": []},
    "cli": {"publish_topics": [], "subscribe_topics": ["news"],
            "serve_methods": [], "call_targets": ["srv"]},
}


def read(path):
    return path.read_text(encoding="utf-8") if path.exists() else ""


def lines(path):
    return [line for line in read(path).splitlines() if line]


def build_broker(tree, directory, secrets_dir, endpoint, log_endpoint):
    msgserv = tree.services / "msgserv"
    generated = directory / "generated"
    config = {"applications": APPLICATIONS}
    metadata = {"applications": [{"application_id": name} for name in APPLICATIONS]}
    registry = {"services": [{"service_id": "msgserv", "executable": "/punos/bin/msgserv",
                              "arguments": [], "environment": [], "enabled": True,
                              "critical": False, "restart": "restart",
                              "terminates_on_sigterm": True}],
                "native_apps": []}
    (directory / "messaging.json").write_text(json.dumps(config), encoding="utf-8")
    (directory / "apps.json").write_text(json.dumps(metadata), encoding="utf-8")
    (directory / "registry.json").write_text(json.dumps(registry), encoding="utf-8")
    (directory / "salt.bin").write_bytes(b"e2e-salt-" * 4)
    subprocess.run(["python3", str(msgserv / "tools/generate_messaging.py"),
                    "--config", str(directory / "messaging.json"),
                    "--metadata", str(directory / "apps.json"),
                    "--registry", str(directory / "registry.json"),
                    "--salt-file", str(directory / "salt.bin"),
                    "--output", str(generated), "--require-service", "msgserv"],
                   check=True)
    environment = json.loads((generated / "environment.json").read_text(encoding="utf-8"))
    secrets_dir.mkdir(exist_ok=True)
    for name, entry in environment.items():
        (secrets_dir / name).write_text(entry.split("=", 1)[1], encoding="utf-8")
    broker = directory / "msgserv"
    compile_and_link(tree,
                     sorted((msgserv / "src").glob("*.c")) + [generated / "messaging_catalog.c"],
                     broker, [msgserv / "include", msgserv / "src", generated],
                     [f'MSG_ENDPOINT="{endpoint}"', f'MSG_LOG_ENDPOINT="{log_endpoint}"'])
    return broker


def build_peer(tree, directory):
    peer = directory / "fw_msg_peer"
    compile_and_link(tree, [tree.appframework / "tests/system/fw_msg_peer.c"], peer,
                     [tree.appframework / "include", tree.appframework / "src"],
                     with_library=True)
    return peer


class Peers:
    """Starts processes with their output in files, so the test reads what they printed."""

    def __init__(self, peer_binary, directory, endpoint, secrets_dir):
        self.binary = peer_binary
        self.directory = directory
        self.endpoint = endpoint
        self.secrets_dir = secrets_dir
        self.processes = {}

    def start(self, name, mode, application_id=None, secret=True):
        environment = dict(os.environ)
        environment.pop("PUNOS_MSG_SECRET", None)
        if secret:
            environment["PUNOS_MSG_SECRET"] = (self.secrets_dir / application_id).read_text(
                encoding="utf-8")
        output = open(self.directory / f"{name}.out", "w", encoding="utf-8")
        process = subprocess.Popen(
            [str(self.binary), mode, self.endpoint, str(self.directory),
             application_id or name],
            stdout=output, stderr=subprocess.STDOUT, env=environment)
        self.processes[name] = (process, output)
        return process

    def output(self, name):
        return lines(self.directory / f"{name}.out")

    def count(self, name, text):
        return sum(1 for line in self.output(name) if line == text or line.startswith(text))

    def stop_all(self):
        for name, (process, output) in self.processes.items():
            if process.poll() is None:
                code = finish(process)
                assert code == 0, f"{name} exited with {code}"
            output.close()


def wait_line(peers, name, text, count=1):
    wait_for(lambda: sum(1 for line in peers.output(name) if line.startswith(text)) >= count,
             f"{name}: {text!r} x{count}")


def scenario(broker_binary, peer_binary, directory, endpoint, secrets_dir):
    broker = {"process": None}

    def start_broker():
        broker["process"] = subprocess.Popen([str(broker_binary)],
                                             stdout=subprocess.DEVNULL,
                                             stderr=subprocess.STDOUT)
        wait_for(lambda: os.path.exists(endpoint), "msgserv endpoint")

    peers = Peers(peer_binary, directory, endpoint, secrets_dir)
    start_broker()
    try:
        peers.start("sub1", "listen", "sub1")
        peers.start("sub2", "listen", "sub2")
        wait_line(peers, "sub1", "linked")
        wait_line(peers, "sub2", "linked")

        peers.start("srv", "serve", "srv")
        wait_for(lambda: (directory / "ready-serve").exists(), "srv is bound")
        peers.start("pub", "publish", "pub")
        wait_line(peers, "pub", "linked")

        ask = peers.start("cli", "ask", "cli")
        wait_for(lambda: ask.poll() is not None, "cli finishes")
        assert ask.returncode == 0, f"cli exited with {ask.returncode}"
        cli = peers.output("cli")
        responses = [line for line in cli if line.startswith("response ")]
        assert responses == ["response error=OK status=7 payload=pong"], responses
        between = cli.index("event news between")
        assert between < cli.index(responses[0]), "event must come before the response"
        srv = peers.output("srv")
        assert srv.index("handler echo") < srv.index("timer respond"), srv
        print("l4: a response comes from a timer; an event between request and response "
              "does not replace it passed")

        (directory / "go1").touch()
        wait_line(peers, "sub1", "event news first")
        wait_line(peers, "sub2", "event news first")
        wait_line(peers, "pub", "published delivered=2")
        print("l4: publish reaches two applications passed")

        finish(broker["process"])
        start_broker()
        wait_line(peers, "sub1", "linked", count=2)
        wait_line(peers, "sub2", "linked", count=2)
        wait_line(peers, "pub", "linked", count=2)
        (directory / "go2").touch()
        wait_line(peers, "sub1", "event news second")
        wait_line(peers, "sub2", "event news second")
        print("l4: subscriptions are restored after msgserv restarts passed")

        dangling = Peers(peer_binary, directory, str(directory / "nobody.sock"), secrets_dir)
        dangling.start("dangling", "dangling", "cli")
        wait_for(lambda: dangling.processes["dangling"][0].poll() is not None,
                 "dangling finishes")
        assert dangling.output("dangling") == ["queued",
                                               "destroy-callback error=INVALID_STATE"], \
            dangling.output("dangling")
        dangling.stop_all()
        print("l4: destroy with a request the broker has not accepted calls its callback "
              "once with CORE_INVALID_STATE passed")

        nosecret = peers.start("nosecret", "nosecret", "lonely", secret=False)
        wait_for(lambda: nosecret.poll() is not None, "nosecret finishes")
        assert peers.output("nosecret") == ["start=UNSUPPORTED"], peers.output("nosecret")
        print("l4: an application without PUNOS_MSG_SECRET gets CORE_UNSUPPORTED passed")
    finally:
        (directory / "stop").touch()
        peers.stop_all()
        if broker["process"] is not None and broker["process"].poll() is None:
            finish(broker["process"])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--punos", required=True)
    parser.add_argument("--library", required=True)
    arguments = parser.parse_args()
    tree = Tree(arguments.punos, arguments.library)
    directory = Path(tempfile.mkdtemp(prefix="appfw-msg-", dir="/tmp"))
    endpoint = str(directory / "msgserv.sock")
    secrets_dir = directory / "secrets"
    broker = build_broker(tree, directory, secrets_dir, endpoint,
                          str(directory / "logserv.sock"))
    peer = build_peer(tree, directory)
    scenario(broker, peer, directory, endpoint, secrets_dir)
    # Каталог с выводом участников остаётся при ошибке: по нему видно, что произошло.
    shutil.rmtree(directory)
    return 0


if __name__ == "__main__":
    sys.exit(main())
