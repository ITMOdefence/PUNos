#!/usr/bin/env python3
"""L3: настоящий процесс msgserv, настоящий ipclib, logserv - тестовый peer.

Сервис собирается здесь же, с подменёнными через -D endpoint'ами, поэтому
тест не трогает /run/punos и не требует прав. Клиенты - обычные
AF_UNIX-сокеты: это одновременно проверяет, что протокол описан точно и
реализуем без appframework.
"""
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
TIMEOUT = 20

BIND, BIND_RESULT = 1, 2
SUBSCRIBE, SUBSCRIBE_RESULT = 3, 4
UNSUBSCRIBE, UNSUBSCRIBE_RESULT = 5, 6
PUBLISH, PUBLISH_RESULT = 7, 8
REQUEST, REQUEST_RESULT = 9, 10
RESPOND, RESPOND_RESULT = 11, 12
EVENT, DELIVER_REQUEST, DELIVER_RESPONSE = 20, 21, 22

CORE_OK = 0
CORE_NOT_FOUND = 5
CORE_ALREADY_EXISTS = 6
CORE_PERMISSION_DENIED = 7
CORE_OVERFLOW = 13

PAYLOAD_MAX = 65536
CONNECTIONS_PER_APPLICATION = 4
APPLICATIONS = 16  # 16 x 4 = MSG_CLIENTS_MAX

SALT = b"integration-salt-for-msgserv-tests"


def secret_of(application_id):
    import hashlib

    return hashlib.sha256(SALT + b"\0" + application_id.encode("utf-8")).hexdigest()


# ---- транспорт -------------------------------------------------------------


def exact(connection, size):
    data = bytearray()
    while len(data) < size:
        part = connection.recv(size - len(data))
        if not part:
            raise EOFError("брокер закрыл соединение")
        data.extend(part)
    return bytes(data)


def frame(message_type, payload=b""):
    return struct.pack("<II", len(payload), message_type) + payload


def receive(connection):
    size, message_type = struct.unpack("<II", exact(connection, 8))
    assert size <= 1048576, size
    return message_type, exact(connection, size)


def text(value):
    encoded = value.encode("utf-8") if isinstance(value, str) else value
    return struct.pack("<H", len(encoded)) + encoded


def blob(payload):
    return struct.pack("<I", len(payload)) + payload


class Reader:
    def __init__(self, data):
        self.data = data
        self.offset = 0

    def fixed(self, fmt):
        value = struct.unpack_from(fmt, self.data, self.offset)
        self.offset += struct.calcsize(fmt)
        return value[0] if len(value) == 1 else value

    def text(self):
        length = self.fixed("<H")
        value = self.data[self.offset:self.offset + length]
        self.offset += length
        return value.decode("utf-8")

    def blob(self):
        length = self.fixed("<I")
        value = self.data[self.offset:self.offset + length]
        self.offset += length
        return value

    def done(self):
        return self.offset == len(self.data)


class Client:
    """Одно соединение с брокером."""

    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.settimeout(TIMEOUT)
        self.socket.connect(str(path))
        self.application_id = None
        self.client_id = None

    def send(self, message_type, payload):
        self.socket.sendall(frame(message_type, payload))

    def expect(self, message_type):
        while True:
            kind, payload = receive(self.socket)
            if kind == message_type:
                return payload
            # Незапрошенные кадры могут прийти между запросом и ответом;
            # тест, который их ждёт, читает их через next_unsolicited().
            assert kind in (EVENT, DELIVER_REQUEST, DELIVER_RESPONSE), kind
            self.pending = getattr(self, "pending", [])
            self.pending.append((kind, payload))

    def next_unsolicited(self, message_type=None):
        queued = getattr(self, "pending", [])
        for index, (kind, payload) in enumerate(queued):
            if message_type is None or kind == message_type:
                queued.pop(index)
                return kind, payload
        while True:
            kind, payload = receive(self.socket)
            if message_type is None or kind == message_type:
                return kind, payload

    def bind(self, application_id, secret=None, error=CORE_OK):
        secret = secret_of(application_id) if secret is None else secret
        self.send(BIND, struct.pack("<H", 1) + text(application_id) + text(secret))
        reader = Reader(self.expect(BIND_RESULT))
        code = reader.fixed("<I")
        assert code == error, (code, error)
        if code == CORE_OK:
            assert reader.text() == application_id
            self.client_id = reader.fixed("<Q")
            self.application_id = application_id
            assert reader.done()
        else:
            assert reader.done(), "в отказе не должно быть полей"
        return self

    def subscribe(self, topic, error=CORE_OK):
        self.send(SUBSCRIBE, struct.pack("<H", 1) + text(topic))
        code = struct.unpack("<I", self.expect(SUBSCRIBE_RESULT))[0]
        assert code == error, (topic, code, error)

    def publish(self, topic, payload, error=CORE_OK):
        self.send(PUBLISH, struct.pack("<H", 1) + text(topic) + blob(payload))
        reader = Reader(self.expect(PUBLISH_RESULT))
        code = reader.fixed("<I")
        assert code == error, (code, error)
        if code != CORE_OK:
            return None, None
        delivered, dropped = reader.fixed("<H"), reader.fixed("<H")
        assert reader.done()
        return delivered, dropped

    def request(self, target, method, payload=b"", error=CORE_OK):
        if isinstance(target, int):
            head = struct.pack("<HBQ", 1, 0, target)
        else:
            head = struct.pack("<HB", 1, 1) + text(target)
        self.send(REQUEST, head + text(method) + blob(payload))
        reader = Reader(self.expect(REQUEST_RESULT))
        code = reader.fixed("<I")
        assert code == error, (code, error)
        if code != CORE_OK:
            return None
        correlation = reader.fixed("<Q")
        assert reader.done()
        return correlation

    def respond(self, correlation, status, payload=b"", error=CORE_OK):
        self.send(RESPOND, struct.pack("<HQI", 1, correlation, status) + blob(payload))
        code = struct.unpack("<I", self.expect(RESPOND_RESULT))[0]
        assert code == error, (code, error)

    def close(self):
        try:
            self.socket.close()
        except OSError:
            pass


# ---- тестовый logserv ------------------------------------------------------


class LogPeer:
    """Принимает записи и следит, что в них нет ни секретов, ни payload."""

    def __init__(self, path, secrets, payloads):
        self.records = []
        self.errors = []
        self.secrets = secrets
        self.payloads = payloads
        self.closed = False
        self.connections = []
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
            threading.Thread(target=self.serve, args=(connection,), daemon=True).start()

    def serve(self, connection):
        try:
            while not self.closed:
                message_type, payload = receive(connection)
                assert message_type == 1, message_type
                version, pid, tid, severity, component_size, message_size = \
                    struct.unpack_from("<HiiBHI", payload)
                assert version == 1 and pid > 0 and tid > 0 and severity in range(6)
                assert len(payload) == 17 + component_size + message_size, payload
                assert payload[17:17 + component_size] == b"msgserv"
                message = payload[17 + component_size:].decode("utf-8")
                for secret in self.secrets:
                    assert secret not in message, "секрет попал в журнал"
                for fragment in self.payloads:
                    assert fragment not in message, "payload попал в журнал"
                self.records.append(message)
                connection.sendall(frame(2, struct.pack("<I", CORE_OK)))
        except (EOFError, OSError):
            pass
        except BaseException as error:  # noqa: BLE001 - поднимается в close()
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
        assert not self.errors, self.errors


# ---- сборка ----------------------------------------------------------------


def build_broker(punos, directory, endpoint, log_endpoint):
    applications = [f"app{index:02d}" for index in range(APPLICATIONS)]
    config = {"applications": {}}
    for name in applications:
        config["applications"][name] = {
            "publish_topics": ["ui.theme", "app.state"],
            "subscribe_topics": ["ui.theme", "app.state"],
            "serve_methods": ["echo", "slow"],
            "call_targets": applications,
        }
    metadata = {"applications": [{"application_id": name} for name in applications]}
    registry = {"services": [{"service_id": "msgserv", "executable": "/punos/bin/msgserv",
                              "arguments": [], "environment": [], "enabled": True,
                              "critical": False, "restart": "restart",
                              "terminates_on_sigterm": True}],
                "native_apps": []}
    (directory / "messaging.json").write_text(json.dumps(config), encoding="utf-8")
    (directory / "apps.json").write_text(json.dumps(metadata), encoding="utf-8")
    (directory / "registry.json").write_text(json.dumps(registry), encoding="utf-8")
    (directory / "salt").write_bytes(SALT)
    subprocess.run(["python3", str(ROOT / "tools/generate_messaging.py"),
                    "--config", str(directory / "messaging.json"),
                    "--metadata", str(directory / "apps.json"),
                    "--registry", str(directory / "registry.json"),
                    "--salt-file", str(directory / "salt"),
                    "--require-service", "msgserv",
                    "--output", str(directory)], check=True)

    libraries = ["core", "mem", "str", "process", "time", "eventloop"]
    includes = [ROOT / "include", ROOT / "src", directory,
                punos / "System/NativeRuntime/SyscallWrappers/include",
                punos / "System/NativeRuntime/include",
                punos / "System/SpecialLibs/ipclib/include"]
    includes += [punos / f"System/SystemLibs/lib{lib}/include" for lib in libraries]
    flags = ["-std=c11", "-ffreestanding", "-nostdinc", "-fno-builtin", "-fno-stack-protector",
             "-fno-pic", "-m64", "-Wall", "-Wextra", "-Werror",
             "-fno-tree-loop-distribute-patterns"]
    flags += [f"-I{path}" for path in includes]
    flags += [f'-DMSG_ENDPOINT="{endpoint}"', f'-DMSG_LOG_ENDPOINT="{log_endpoint}"']
    archives = [punos / f"System/SystemLibs/lib{lib}/build/libpunos_{lib}.a"
                for lib in libraries]
    archives += [punos / "System/SpecialLibs/ipclib/build/libpunos_special_ipc.a",
                 punos / "System/NativeRuntime/CRT/build/libpunos_crt.a",
                 punos / "System/NativeRuntime/SyscallWrappers/build/libpunos_syscall.a"]
    objects = []
    for name in ("broker", "logger", "main"):
        obj = directory / f"{name}.o"
        subprocess.run(["cc", *flags, "-c", str(ROOT / f"src/{name}.c"), "-o", str(obj)],
                       check=True)
        objects.append(obj)
    catalog = directory / "messaging_catalog.o"
    subprocess.run(["cc", *flags, "-c", str(directory / "messaging_catalog.c"),
                    "-o", str(catalog)], check=True)
    objects.append(catalog)
    executable = directory / "msgserv"
    subprocess.run(["cc", "-nostdlib", "-no-pie", "-Wl,-e,_start",
                    str(punos / "System/NativeRuntime/CRT/build/crt0.o"),
                    *map(str, objects), "-Wl,--start-group", *map(str, archives),
                    "-Wl,--end-group", "-o", str(executable)], check=True)
    return executable, applications


def wait_for_socket(path):
    deadline = time.monotonic() + TIMEOUT
    while time.monotonic() < deadline:
        if Path(path).exists():
            try:
                probe = socket.socket(socket.AF_UNIX)
                probe.connect(str(path))
                probe.close()
                return
            except OSError:
                pass
        time.sleep(0.02)
    raise AssertionError(f"брокер не поднял endpoint {path}")


# ---- сценарии --------------------------------------------------------------


def scenario_identity(endpoint, applications):
    """Секрет подтверждает личность; подделать application_id нельзя."""
    good = Client(endpoint).bind(applications[0])
    assert good.client_id > 0
    Client(endpoint).bind(applications[1], secret="не тот секрет",
                          error=CORE_PERMISSION_DENIED).close()
    Client(endpoint).bind("нет такого", secret="x", error=CORE_NOT_FOUND).close()
    # Секрет один на приложение, поэтому число соединений ограничено явно.
    extra = [Client(endpoint).bind(applications[0])
             for _ in range(CONNECTIONS_PER_APPLICATION - 1)]
    Client(endpoint).bind(applications[0], error=CORE_OVERFLOW).close()
    identifiers = {good.client_id, *(client.client_id for client in extra)}
    assert len(identifiers) == CONNECTIONS_PER_APPLICATION, "client_id повторился"
    for client in (good, *extra):
        client.close()


def scenario_publish(endpoint, applications):
    """Рассылка, поля отправителя от брокера и sequence по topic'у."""
    publisher = Client(endpoint).bind(applications[0])
    first = Client(endpoint).bind(applications[1])
    second = Client(endpoint).bind(applications[2])
    for subscriber in (first, second):
        subscriber.subscribe("ui.theme")
    delivered, dropped = publisher.publish("ui.theme", b"payload-one")
    assert (delivered, dropped) == (2, 0), (delivered, dropped)
    for subscriber in (first, second):
        kind, payload = subscriber.next_unsolicited(EVENT)
        reader = Reader(payload)
        assert reader.fixed("<Q") == publisher.client_id
        assert reader.text() == applications[0]
        assert reader.text() == "ui.theme"
        assert reader.fixed("<Q") == 1
        assert reader.blob() == b"payload-one"
        assert reader.done()
    publisher.publish("ui.theme", b"payload-two")
    for subscriber in (first, second):
        reader = Reader(subscriber.next_unsolicited(EVENT)[1])
        reader.fixed("<Q"), reader.text(), reader.text()
        assert reader.fixed("<Q") == 2, "sequence не вырос"
    # Topic без права публикации у этого приложения не существует в тестовом
    # каталоге, поэтому проверяем неизвестный topic.
    publisher.publish("ui.unknown", b"x", error=CORE_PERMISSION_DENIED)
    first.close()
    second.close()
    publisher.close()


def scenario_request(endpoint, applications):
    """Запрос, ответ и разделение транспортной ошибки с прикладным статусом."""
    caller = Client(endpoint).bind(applications[0])
    server = Client(endpoint).bind(applications[1])
    correlation = caller.request(server.client_id, "echo", b"ping")
    kind, payload = server.next_unsolicited(DELIVER_REQUEST)
    reader = Reader(payload)
    assert reader.fixed("<Q") == correlation
    assert reader.fixed("<Q") == caller.client_id
    assert reader.text() == applications[0]
    assert reader.text() == "echo"
    assert reader.blob() == b"ping"
    assert reader.done()
    server.respond(correlation, 77, b"pong")
    reader = Reader(caller.next_unsolicited(DELIVER_RESPONSE)[1])
    assert reader.fixed("<Q") == correlation
    assert reader.fixed("<I") == CORE_OK
    assert reader.fixed("<I") == 77
    assert reader.blob() == b"pong"
    assert reader.done()

    # Адресация по application_id: один экземпляр - работает, два - нет.
    assert caller.request(applications[1], "echo") is not None
    reader = Reader(server.next_unsolicited(DELIVER_REQUEST)[1])
    second = Client(endpoint).bind(applications[1])
    caller.request(applications[1], "echo", error=CORE_ALREADY_EXISTS)
    second.close()

    # Метод, которого адресат не обслуживает.
    caller.request(server.client_id, "missing", error=CORE_PERMISSION_DENIED)
    caller.close()
    server.close()


def scenario_dead_target(endpoint, applications):
    """Адресат убит: отправитель узнаёт сразу, а не по таймауту."""
    caller = Client(endpoint).bind(applications[0])
    server = Client(endpoint).bind(applications[1])
    correlation = caller.request(server.client_id, "slow")
    server.next_unsolicited(DELIVER_REQUEST)
    server.socket.close()  # обрыв без ответа
    reader = Reader(caller.next_unsolicited(DELIVER_RESPONSE)[1])
    assert reader.fixed("<Q") == correlation
    assert reader.fixed("<I") == CORE_NOT_FOUND
    caller.close()


def scenario_silent_client(endpoint, applications):
    """Клиент, не читающий сокет, не тормозит брокера."""
    silent = Client(endpoint).bind(applications[3])
    silent.subscribe("app.state")
    active = Client(endpoint).bind(applications[4])
    active.subscribe("app.state")
    publisher = Client(endpoint).bind(applications[5])
    big = bytes(PAYLOAD_MAX)
    dropped_seen = False
    for _ in range(200):
        delivered, dropped = publisher.publish("app.state", big)
        if dropped:
            dropped_seen = True
        # Активный подписчик продолжает получать события, пока молчащий
        # упирается в свою очередь.
        active.next_unsolicited(EVENT)
        if dropped_seen:
            break
    assert dropped_seen, "очередь молчащего клиента не переполнилась"
    # Брокер отвечает на новые запросы, несмотря на забитую очередь.
    assert publisher.publish("ui.theme", b"still alive")[0] is not None
    silent.close()
    active.close()
    publisher.close()


def scenario_many_events(endpoint, applications):
    """10 000 событий подряд доходят без потерь, если подписчик читает."""
    publisher = Client(endpoint).bind(applications[6])
    subscriber = Client(endpoint).bind(applications[7])
    subscriber.subscribe("ui.theme")
    total = 10000
    received = 0
    reader_thread_error = []

    def drain():
        nonlocal received
        try:
            while received < total:
                kind, payload = receive(subscriber.socket)
                assert kind == EVENT, kind
                reader = Reader(payload)
                reader.fixed("<Q"), reader.text(), reader.text()
                sequence = reader.fixed("<Q")
                received += 1
                assert sequence == received, (sequence, received)
        except BaseException as error:  # noqa: BLE001
            reader_thread_error.append(error)

    thread = threading.Thread(target=drain, daemon=True)
    thread.start()
    for index in range(total):
        publisher.publish("ui.theme", struct.pack("<I", index))
    thread.join(timeout=TIMEOUT * 3)
    assert not reader_thread_error, reader_thread_error
    assert received == total, received
    publisher.close()
    subscriber.close()


def scenario_full_fanout(endpoint, applications):
    """Полный fan-out: 64 КиБ на MSG_CLIENTS_MAX - 1 подписчиков."""
    clients = []
    for name in applications:
        for _ in range(CONNECTIONS_PER_APPLICATION):
            clients.append(Client(endpoint).bind(name))
    assert len(clients) == APPLICATIONS * CONNECTIONS_PER_APPLICATION == 64
    publisher = clients[0]
    subscribers = clients[1:]
    for subscriber in subscribers:
        subscriber.subscribe("ui.theme")
    big = bytes(PAYLOAD_MAX)
    results = []
    threads = []

    def drain(subscriber):
        kind, payload = receive(subscriber.socket)
        results.append((kind, len(payload)))

    for subscriber in subscribers:
        thread = threading.Thread(target=drain, args=(subscriber,), daemon=True)
        thread.start()
        threads.append(thread)
    delivered, dropped = publisher.publish("ui.theme", big)
    assert (delivered, dropped) == (len(subscribers), 0), (delivered, dropped)
    for thread in threads:
        thread.join(timeout=TIMEOUT)
    assert len(results) == len(subscribers), len(results)
    assert all(kind == EVENT for kind, _ in results)
    # Больше MSG_CLIENTS_MAX брокер не принимает: соединение закрывается.
    refused = socket.socket(socket.AF_UNIX)
    refused.settimeout(TIMEOUT)
    refused.connect(str(endpoint))
    # Брокер закрывает лишнее соединение, не читая bind, поэтому ядро может
    # ответить нулевым чтением, RST или оборвать уже саму запись.
    try:
        refused.sendall(frame(BIND, struct.pack("<H", 1) + text(applications[0]) +
                              text(secret_of(applications[0]))))
        assert refused.recv(8) == b"", "лишнее соединение не закрыто"
    except (ConnectionResetError, BrokenPipeError):
        pass
    refused.close()
    for client in clients:
        client.close()


def scenario_restart(punos, directory, endpoint, log_endpoint, applications, executable):
    """Перезапуск брокера: endpoint снова доступен, подписки надо завести заново."""
    process = subprocess.Popen([str(executable)], cwd=str(directory))
    wait_for_socket(endpoint)
    client = Client(endpoint).bind(applications[0])
    client.subscribe("ui.theme")
    client.close()
    process.send_signal(signal.SIGTERM)
    assert process.wait(timeout=TIMEOUT) == 0, process.returncode
    process = subprocess.Popen([str(executable)], cwd=str(directory))
    wait_for_socket(endpoint)
    again = Client(endpoint).bind(applications[0])
    # Подписки не переживают перезапуск: брокер без персистентности.
    again.subscribe("ui.theme")
    again.close()
    return process


def run(punos, build):
    with tempfile.TemporaryDirectory(prefix="msgserv-") as temporary:
        directory = Path(temporary)
        endpoint = directory / "msgserv.sock"
        log_endpoint = directory / "logserv.sock"
        executable, applications = build_broker(punos, directory, endpoint, log_endpoint)
        secrets = [secret_of(name) for name in applications]
        log = LogPeer(log_endpoint, secrets, ["payload-one", "payload-two", "pong"])
        process = subprocess.Popen([str(executable)], cwd=str(directory))
        try:
            wait_for_socket(endpoint)
            for name, scenario in (("identity", scenario_identity),
                                   ("publish", scenario_publish),
                                   ("request", scenario_request),
                                   ("dead_target", scenario_dead_target),
                                   ("silent_client", scenario_silent_client),
                                   ("many_events", scenario_many_events),
                                   ("full_fanout", scenario_full_fanout)):
                started = time.monotonic()
                scenario(endpoint, applications)
                print(f"  {name}: ок за {time.monotonic() - started:.1f} c", flush=True)
            # Брокер пережил всё перечисленное.
            assert process.poll() is None, f"msgserv упал: {process.returncode}"
            process.send_signal(signal.SIGTERM)
            assert process.wait(timeout=TIMEOUT) == 0, process.returncode
            process = scenario_restart(punos, directory, endpoint, log_endpoint,
                                       applications, executable)
            process.send_signal(signal.SIGTERM)
            assert process.wait(timeout=TIMEOUT) == 0, process.returncode
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=TIMEOUT)
            log.close()
        assert any("ready" in record for record in log.records), log.records
        assert any("bind" in record for record in log.records), log.records
        print(f"L3: все сценарии прошли, записей в журнале: {len(log.records)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--punos", required=True, type=Path)
    parser.add_argument("--build", required=True, type=Path)
    arguments = parser.parse_args()
    run(arguments.punos.resolve(), arguments.build.resolve())


if __name__ == "__main__":
    main()
