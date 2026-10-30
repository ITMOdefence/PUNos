#!/usr/bin/env python3
"""Тесты генератора каталога прав и секретов.

Генератор - единственное место, где нарушение прав обязано стать ошибкой
сборки, а не предупреждением, поэтому каждое правило проверяется отдельно.
"""
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "tools/generate_messaging.py"
SALT = b"a" * 32

REGISTRY = {"services": [{"service_id": "msgserv", "executable": "/punos/bin/msgserv",
                          "arguments": [], "environment": [], "enabled": True,
                          "critical": False, "restart": "restart",
                          "terminates_on_sigterm": True}],
            "native_apps": []}

CONFIG = {"applications": {
    "gallery": {"publish_topics": ["app.state"], "subscribe_topics": ["ui.theme"],
                "serve_methods": ["open_image"], "call_targets": ["settings"]},
    "settings": {"publish_topics": ["ui.theme"], "subscribe_topics": ["app.state"],
                 "serve_methods": ["get_value"], "call_targets": ["gallery"]}}}

METADATA = {"applications": [{"application_id": "gallery"}, {"application_id": "settings"}]}


class GeneratorCase(unittest.TestCase):
    def run_generator(self, config=None, metadata=None, registry=None, salt=SALT,
                      require_service="msgserv"):
        directory = Path(tempfile.mkdtemp(prefix="msgserv-generator-"))
        (directory / "messaging.json").write_text(
            json.dumps(CONFIG if config is None else config), encoding="utf-8")
        (directory / "apps.json").write_text(
            json.dumps(METADATA if metadata is None else metadata), encoding="utf-8")
        (directory / "registry.json").write_text(
            json.dumps(REGISTRY if registry is None else registry), encoding="utf-8")
        (directory / "salt").write_bytes(salt)
        arguments = [sys.executable, str(GENERATOR),
                     "--config", str(directory / "messaging.json"),
                     "--metadata", str(directory / "apps.json"),
                     "--registry", str(directory / "registry.json"),
                     "--salt-file", str(directory / "salt"),
                     "--output", str(directory / "out")]
        if require_service:
            arguments += ["--require-service", require_service]
        result = subprocess.run(arguments, capture_output=True, text=True)
        return result, directory / "out"

    def expect_failure(self, message, **kwargs):
        result, _ = self.run_generator(**kwargs)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("не удалась", result.stderr, result.stderr)
        self.assertIn(message, result.stderr, result.stderr)

    # ---- успешный случай --------------------------------------------------

    def test_generates_catalog_and_environment(self):
        result, output = self.run_generator()
        self.assertEqual(result.returncode, 0, result.stderr)
        source = (output / "messaging_catalog.c").read_text(encoding="utf-8")
        header = (output / "messaging_catalog.h").read_text(encoding="utf-8")
        self.assertIn("#define MSG_CATALOG_COUNT 2", header)
        self.assertIn("const MsgCatalog msg_generated_catalog", source)
        # Строки записаны восьмеричными escape'ами фиксированной ширины.
        self.assertIn("\\147\\141\\154\\154\\145\\162\\171", source)  # gallery
        environment = json.loads((output / "environment.json").read_text(encoding="utf-8"))
        self.assertEqual(set(environment), {"gallery", "settings"})
        for application_id, entry in environment.items():
            digest = hashlib.sha256(SALT + b"\0" + application_id.encode()).hexdigest()
            self.assertEqual(entry, f"PUNOS_MSG_SECRET={digest}")
            escaped = "".join(f"\\{byte:03o}" for byte in digest.encode())
            self.assertIn(escaped, source, "секрет приложения не попал в каталог")

    def test_secret_depends_on_salt_and_is_stable(self):
        first, first_output = self.run_generator()
        second, second_output = self.run_generator()
        self.assertEqual(first.returncode, 0)
        self.assertEqual(second.returncode, 0)
        self.assertEqual((first_output / "messaging_catalog.c").read_text(),
                         (second_output / "messaging_catalog.c").read_text(),
                         "одна соль обязана давать один каталог")
        other, other_output = self.run_generator(salt=b"b" * 32)
        self.assertEqual(other.returncode, 0)
        self.assertNotEqual((first_output / "environment.json").read_text(),
                            (other_output / "environment.json").read_text())

    def test_order_of_applications_does_not_change_output(self):
        reordered = {"applications": dict(reversed(list(CONFIG["applications"].items())))}
        first, first_output = self.run_generator()
        second, second_output = self.run_generator(config=reordered)
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertEqual((first_output / "messaging_catalog.c").read_text(),
                         (second_output / "messaging_catalog.c").read_text())

    # ---- отказы -----------------------------------------------------------

    def test_unknown_application(self):
        config = json.loads(json.dumps(CONFIG))
        config["applications"]["ghost"] = {"publish_topics": [], "subscribe_topics": [],
                                           "serve_methods": [], "call_targets": []}
        self.expect_failure("нет такого приложения", config=config)

    def test_unknown_permission_field(self):
        config = json.loads(json.dumps(CONFIG))
        config["applications"]["gallery"]["wildcard_topics"] = ["ui.*"]
        self.expect_failure("неизвестные поля прав", config=config)

    def test_duplicate_topic(self):
        config = json.loads(json.dumps(CONFIG))
        config["applications"]["gallery"]["subscribe_topics"] = ["ui.theme", "ui.theme"]
        self.expect_failure("повторяющееся значение", config=config)

    def test_topic_too_long(self):
        config = json.loads(json.dumps(CONFIG))
        config["applications"]["gallery"]["subscribe_topics"] = ["t" * 129]
        self.expect_failure("длиннее 128 байт", config=config)

    def test_too_many_subscriptions(self):
        config = json.loads(json.dumps(CONFIG))
        config["applications"]["gallery"]["subscribe_topics"] = [f"t{i}" for i in range(33)]
        self.expect_failure("MSG_SUBSCRIPTIONS_MAX_PER_CLIENT", config=config)

    def test_call_target_not_described(self):
        config = json.loads(json.dumps(CONFIG))
        config["applications"]["gallery"]["call_targets"] = ["settings", "gallery2"]
        self.expect_failure("не описан в messaging.json", config=config)

    def test_call_target_serves_nothing(self):
        config = json.loads(json.dumps(CONFIG))
        config["applications"]["settings"]["serve_methods"] = []
        self.expect_failure("не обслуживает методов", config=config)

    def test_duplicate_json_key(self):
        directory = Path(tempfile.mkdtemp(prefix="msgserv-generator-"))
        (directory / "messaging.json").write_text(
            '{"applications": {"gallery": {}, "gallery": {}}}', encoding="utf-8")
        (directory / "apps.json").write_text(json.dumps(METADATA), encoding="utf-8")
        (directory / "registry.json").write_text(json.dumps(REGISTRY), encoding="utf-8")
        (directory / "salt").write_bytes(SALT)
        result = subprocess.run(
            [sys.executable, str(GENERATOR),
             "--config", str(directory / "messaging.json"),
             "--metadata", str(directory / "apps.json"),
             "--registry", str(directory / "registry.json"),
             "--salt-file", str(directory / "salt"),
             "--output", str(directory / "out")], capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn("повторяющийся ключ JSON", result.stderr)

    def test_registry_without_service(self):
        self.expect_failure("нет сервиса msgserv", registry={"services": [], "native_apps": []})

    def test_registry_service_disabled(self):
        registry = json.loads(json.dumps(REGISTRY))
        registry["services"][0]["enabled"] = False
        self.expect_failure("выключен в реестре", registry=registry)

    def test_registry_service_critical(self):
        # critical: true означает, что отказ брокера рушит загрузку системы.
        registry = json.loads(json.dumps(REGISTRY))
        registry["services"][0]["critical"] = True
        self.expect_failure("critical: false", registry=registry)

    def test_short_salt(self):
        self.expect_failure("соль короче 32 байт", salt=b"short")


class SnapshotCase(unittest.TestCase):
    """Реестр и каталог приложений берутся из дерева PUNos напрямую, копий нет.
    Проверяем, что реестр объявляет msgserv и что каталог прав собирается
    против него с тем же конфигом, что лежит в репозитории."""

    def test_platform_registry_declares_msgserv(self):
        registry = ROOT.parent / "platform_registry.json"
        document = json.loads(registry.read_text(encoding="utf-8"))
        services = [service for service in document["services"]
                    if service["service_id"] == "msgserv"]
        self.assertEqual(len(services), 1, "msgserv должен быть записан в реестре PUNos")
        self.assertEqual(services[0]["executable"], "/punos/bin/msgserv")
        self.assertFalse(services[0]["critical"])

    def test_repository_config_builds(self):
        result = subprocess.run(
            [sys.executable, str(GENERATOR),
             "--config", str(ROOT / "config/messaging.json"),
             "--metadata", str(ROOT.parent / "applications.json"),
             "--registry", str(ROOT.parent / "platform_registry.json"),
             "--salt-file", "/dev/stdin",
             "--require-service", "msgserv",
             "--output", str(Path(tempfile.mkdtemp(prefix="msgserv-repo-")))],
            input=SALT, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr.decode())


if __name__ == "__main__":
    unittest.main()
