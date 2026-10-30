#!/usr/bin/env python3
"""Регрессионные мутации брокера msgserv.

Каждая мутация ломает одно правило спецификации в копии src/broker.c. Тесты
обязаны упасть: ненулевой код выхода make и запущенная тестовая программа,
которая не прошла. Ошибка сборки мутанта не считается обнаружением.

Запуск:  python3 tests/check_mutations.py --punos /path/to/PUNos
(дерево PUNos должно содержать собранные библиотеки или быть собираемым).
"""
import argparse
from dataclasses import dataclass
from pathlib import Path
import shutil
import subprocess
import tempfile

BROKER = "src/broker.c"
TESTS = ["check-unit", "check-protocol", "check-faults"]


@dataclass(frozen=True)
class Mutation:
    name: str
    changes: tuple

    def apply(self, text):
        for old, new in self.changes:
            if text.count(old) != 1:
                raise ValueError(f"{self.name}: expected exactly one match: {old!r}")
            text = text.replace(old, new, 1)
        return text


MUTATIONS = [
    Mutation("sequence does not grow on a discarded frame", (
        ("        } else {\n            dropped++;\n        }\n    }\n    (void)memory_free(&block);",
         "        } else {\n            dropped++;\n            subscription->sequence--;\n        }\n    }\n    (void)memory_free(&block);"),)),
    Mutation("subscriber without topic permission gets events", (
        ("        if (!list_contains(peer->entry->subscribe_topics, peer->entry->subscribe_topic_count,",
         "        if (false && !list_contains(peer->entry->subscribe_topics, peer->entry->subscribe_topic_count,"),)),
    Mutation("publisher receives its own event", (
        ("if (peer == client || peer->entry == NULL) {", "if (peer->entry == NULL) {"),)),
    Mutation("respond accepted from a foreign connection", (
        ("            if (pending->target != client->id) {", "            if (false && pending->target != client->id) {"),)),
    Mutation("bind timeout does not close the connection", (
        ("            broker_log(broker, \"bind timeout\", NULL, CORE_TIMEOUT);\n            msg_broker_close_client(broker, link);",
         "            broker_log(broker, \"bind timeout\", NULL, CORE_TIMEOUT);\n            link = &client->next;"),)),
    Mutation("request deadline does not produce CORE_TIMEOUT", (
        ("(void)pending_finish(client, pending, CORE_TIMEOUT, 0, NULL, 0);",
         "(void)pending_finish(client, pending, CORE_OK, 0, NULL, 0);"),)),
    Mutation("no application and wrong secret are indistinguishable", (
        ("        error = CORE_PERMISSION_DENIED;\n    } else if (connections_of",
         "        error = CORE_NOT_FOUND;\n    } else if (connections_of"),)),
    Mutation("MSG_CONNECTIONS_MAX_PER_APPLICATION off by one", (
        ("connections_of(broker, entry) >= MSG_CONNECTIONS_MAX_PER_APPLICATION",
         "connections_of(broker, entry) > MSG_CONNECTIONS_MAX_PER_APPLICATION"),)),
]


def run_make(tree, punos):
    result = subprocess.run(["make", "-C", str(tree), f"PUNOS_DIR={punos}", *TESTS],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return result.returncode, result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--punos", type=Path, required=True)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    punos = args.punos.resolve()
    with tempfile.TemporaryDirectory(prefix="msgserv-mutations-") as directory:
        tree = Path(directory) / "msgserv"
        shutil.copytree(args.source, tree,
                        ignore=shutil.ignore_patterns("build", ".git", "__pycache__",
                                                      "secret.salt"))
        status, output = run_make(tree, punos)
        if status:
            raise RuntimeError(f"Baseline failed:\n{output[-3000:]}")
        for mutation in MUTATIONS:
            source = tree / BROKER
            original = source.read_text(encoding="utf-8")
            try:
                source.write_text(mutation.apply(original), encoding="utf-8")
                status, output = run_make(tree, punos)
                broke_build = " error: " in output or "undefined reference" in output
                if status == 0 or broke_build:
                    raise RuntimeError(f"{mutation.name}: survived or broke the build:\n"
                                       f"{output[-3000:]}")
                failed = [line for line in output.splitlines()
                          if "failed" in line or "Segmentation" in line or "Aborted" in line]
                print(f"{mutation.name}: detected "
                      f"({failed[0].strip() if failed else 'test exit'})", flush=True)
            finally:
                source.write_text(original, encoding="utf-8")
        status, output = run_make(tree, punos)
        if status:
            raise RuntimeError(f"Restored tree failed:\n{output[-3000:]}")
    print(f"All {len(MUTATIONS)} mutations detected.")


if __name__ == "__main__":
    main()
