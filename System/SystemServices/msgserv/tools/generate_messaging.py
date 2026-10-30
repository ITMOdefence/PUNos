#!/usr/bin/env python3
"""Сборочный генератор каталога прав и секретов msgserv.

Читает config/messaging.json вместе с копиями platform_registry.json и
applications.json, проверяет каждое правило спецификации и выдаёт:

  * messaging_catalog.c и messaging_catalog.h -- каталог для брокера;
  * environment.json -- записи PUNOS_MSG_SECRET=... для applications.json.

Нарушение -- ошибка сборки, а не предупреждение: брокер с неполным каталогом
хуже отсутствующего брокера.

Секрет приложения выводится из солёного хэша его application_id:

    secret = sha256(salt || 0x00 || application_id).hexdigest()

Соль лежит в файле вне репозитория и создаётся командой make secret. Одна и
та же соль даёт один и тот же образ, поэтому воспроизводимость initramfs не
страдает; новая соль обнуляет все выданные секреты.
"""
import argparse
import hashlib
import json
from pathlib import Path

# Должны совпадать с include/msgserv.h.
APPLICATION_ID_MAX_LENGTH = 64
TOPIC_MAX_LENGTH = 128
METHOD_MAX_LENGTH = 64
SUBSCRIPTIONS_MAX_PER_CLIENT = 32
CLIENTS_MAX = 64
SECRET_MAX_LENGTH = 128

PERMISSION_FIELDS = ("publish_topics", "subscribe_topics", "serve_methods", "call_targets")
LIMITS = {
    "publish_topics": TOPIC_MAX_LENGTH,
    "subscribe_topics": TOPIC_MAX_LENGTH,
    "serve_methods": METHOD_MAX_LENGTH,
    "call_targets": APPLICATION_ID_MAX_LENGTH,
}


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"повторяющийся ключ JSON: {key}")
        result[key] = value
    return result


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"), object_pairs_hook=unique_object)


def text(value, name, limit, nonempty=True):
    if not isinstance(value, str) or "\0" in value:
        raise ValueError(f"{name}: ожидается UTF-8 без NUL")
    encoded = value.encode("utf-8")
    if not encoded and nonempty:
        raise ValueError(f"{name}: пустая строка")
    if len(encoded) > limit:
        raise ValueError(f"{name}: длиннее {limit} байт")
    return value


def c_string(value):
    # Восьмеричные escape фиксированной ширины не зависят от кодировки
    # исходника и не могут проглотить следующую цифру.
    return '"' + "".join(f"\\{byte:03o}" for byte in value.encode("utf-8")) + '"'


def string_initializer(value):
    return "{ (char *)" + c_string(value) + f", {len(value.encode('utf-8'))} }}"


def secret_of(salt, application_id):
    digest = hashlib.sha256(salt + b"\0" + application_id.encode("utf-8")).hexdigest()
    if len(digest) > SECRET_MAX_LENGTH:
        raise ValueError("секрет длиннее MSG_SECRET_MAX_LENGTH")
    return digest


def validate(config, metadata, registry, require_service):
    if not isinstance(config, dict) or set(config) != {"applications"} or \
            not isinstance(config["applications"], dict):
        raise ValueError("messaging.json: ожидается объект applications")
    if not isinstance(metadata, dict) or not isinstance(metadata.get("applications"), list):
        raise ValueError("applications.json: ожидается массив applications")

    known = []
    for app in metadata["applications"]:
        if not isinstance(app, dict) or "application_id" not in app:
            raise ValueError("applications.json: записи без application_id")
        known.append(text(app["application_id"], "application_id", APPLICATION_ID_MAX_LENGTH))
    if len(set(known)) != len(known):
        raise ValueError("applications.json: повторяющийся application_id")

    if require_service:
        services = registry.get("services") if isinstance(registry, dict) else None
        if not isinstance(services, list):
            raise ValueError("platform_registry.json: ожидается массив services")
        entry = next((item for item in services
                      if isinstance(item, dict) and item.get("service_id") == require_service), None)
        if entry is None:
            raise ValueError(f"в реестре нет сервиса {require_service}")
        if entry.get("enabled") is not True:
            raise ValueError(f"сервис {require_service} выключен в реестре")
        if entry.get("critical") is not False:
            raise ValueError(f"сервис {require_service} обязан быть critical: false")

    applications = config["applications"]
    if len(applications) > CLIENTS_MAX:
        raise ValueError(f"больше {CLIENTS_MAX} приложений в messaging.json")
    validated = {}
    for application_id, rights in applications.items():
        text(application_id, "application_id", APPLICATION_ID_MAX_LENGTH)
        if application_id not in known:
            raise ValueError(f"{application_id}: нет такого приложения в applications.json")
        if not isinstance(rights, dict) or set(rights) - set(PERMISSION_FIELDS):
            raise ValueError(f"{application_id}: неизвестные поля прав")
        entry = {}
        for field in PERMISSION_FIELDS:
            values = rights.get(field, [])
            if not isinstance(values, list):
                raise ValueError(f"{application_id}.{field}: ожидается массив")
            for value in values:
                text(value, f"{application_id}.{field}", LIMITS[field])
            if len(set(values)) != len(values):
                raise ValueError(f"{application_id}.{field}: повторяющееся значение")
            entry[field] = sorted(values)
        if len(entry["subscribe_topics"]) > SUBSCRIPTIONS_MAX_PER_CLIENT:
            raise ValueError(f"{application_id}: подписок больше "
                             f"MSG_SUBSCRIPTIONS_MAX_PER_CLIENT")
        validated[application_id] = entry
    for application_id, entry in validated.items():
        for target in entry["call_targets"]:
            if target not in validated:
                raise ValueError(f"{application_id}: адресат {target} не описан в messaging.json")
            if not validated[target]["serve_methods"]:
                raise ValueError(f"{application_id}: адресат {target} не обслуживает методов")
    return validated


def generate(validated, salt):
    header = ["/* Сгенерировано из config/messaging.json. Не редактировать. */",
              "#ifndef PUNOS_MSGSERV_CATALOG_GENERATED_H",
              "#define PUNOS_MSGSERV_CATALOG_GENERATED_H",
              "#include <msgserv.h>",
              f"#define MSG_CATALOG_COUNT {len(validated)}",
              "#endif", ""]
    source = ["/* Сгенерированный каталог прав msgserv. Не редактировать. */",
              '#include "messaging_catalog.h"']
    order = sorted(validated)
    for index, application_id in enumerate(order):
        entry = validated[application_id]
        for field in PERMISSION_FIELDS:
            if entry[field]:
                source.append(f"static const String app_{index}_{field}[] = {{")
                source.extend("    " + string_initializer(value) + "," for value in entry[field])
                source.append("};")
    if order:
        source.append("static const MsgAppEntry catalog_entries[] = {")
        for index, application_id in enumerate(order):
            entry = validated[application_id]
            fields = [string_initializer(application_id),
                      string_initializer(secret_of(salt, application_id))]
            for field in PERMISSION_FIELDS:
                fields += [f"app_{index}_{field}" if entry[field] else "NULL",
                           str(len(entry[field]))]
            source.append("    { " + ", ".join(fields) + " },")
        source.append("};")
    source += ["const MsgCatalog msg_generated_catalog = {",
               "    catalog_entries, MSG_CATALOG_COUNT" if order else "    NULL, 0",
               "};", ""]
    environment = {application_id: f"PUNOS_MSG_SECRET={secret_of(salt, application_id)}"
                   for application_id in order}
    return "\n".join(header), "\n".join(source), environment


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", required=True)
    parser.add_argument("--metadata", required=True)
    parser.add_argument("--registry", required=True)
    parser.add_argument("--salt-file", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--require-service", default="")
    args = parser.parse_args()
    try:
        salt = Path(args.salt_file).read_bytes()
        if len(salt) < 32:
            raise ValueError("соль короче 32 байт: пересоздайте её через make secret")
        validated = validate(read_json(args.config), read_json(args.metadata),
                             read_json(args.registry), args.require_service)
        header, source, environment = generate(validated, salt)
    except (ValueError, UnicodeError, OSError) as error:
        parser.exit(1, f"генерация каталога msgserv не удалась: {error}\n")
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    (output / "messaging_catalog.h").write_text(header, encoding="utf-8")
    (output / "messaging_catalog.c").write_text(source, encoding="utf-8")
    (output / "environment.json").write_text(
        json.dumps(environment, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8")


if __name__ == "__main__":
    main()
