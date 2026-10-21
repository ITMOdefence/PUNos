#!/usr/bin/env python3
"""Build-time adapter for initserv's PlatformRegistry; no runtime manifests."""
import argparse
import hashlib
import json
from pathlib import Path


def text(value, name, limit=1048576, nonempty=False):
    if not isinstance(value, str) or "\0" in value:
        raise ValueError(f"{name}: expected UTF-8 string without NUL")
    encoded = value.encode("utf-8")
    if len(encoded) > limit or (nonempty and not encoded):
        raise ValueError(f"{name}: invalid byte length")
    return value


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"),
                      object_pairs_hook=unique_object)


def c_string(value):
    # Fixed-width octal escapes are independent of compiler source encoding
    # and cannot swallow a following hexadecimal digit.
    return '"' + "".join(f"\\{byte:03o}" for byte in value.encode("utf-8")) + '"'


def string_initializer(value):
    return "{ (char *)" + c_string(value) + f", {len(value.encode('utf-8'))} }}"


def generate(registry, metadata):
    if not isinstance(registry, dict) or not isinstance(registry.get("native_apps"), list):
        raise ValueError("PlatformRegistry must contain a native_apps array")
    if not isinstance(metadata, dict) or set(metadata) != {"applications"} or \
            not isinstance(metadata["applications"], list):
        raise ValueError("metadata must contain an applications array")
    launches = registry["native_apps"]
    apps = metadata["applications"]
    if len(launches) > 64 or len(apps) != len(launches):
        raise ValueError("catalogue count mismatch or more than 64 applications")
    allowed = {}
    for launch in launches:
        if not isinstance(launch, dict) or set(launch) != {"application_id", "executable"}:
            raise ValueError("NativeAppLaunchSpec requires application_id and executable")
        app_id = text(launch["application_id"], "application_id", 64, True)
        executable = text(launch["executable"], "executable", nonempty=True)
        if app_id in allowed:
            raise ValueError("duplicate application_id in PlatformRegistry")
        allowed[app_id] = executable
    validated = {}
    required = {"application_id", "display_name", "executable", "default_arguments",
                "environment", "terminates_on_sigterm"}
    for app in apps:
        if not isinstance(app, dict) or set(app) != required:
            raise ValueError("NativeAppSpec fields do not match the build schema")
        app_id = text(app["application_id"], "application_id", 64, True)
        text(app["display_name"], "display_name", 64)
        text(app["executable"], "executable", nonempty=True)
        if app_id in validated or app_id not in allowed or \
                app["executable"] != allowed[app_id]:
            raise ValueError("application_id/executable mismatch or duplicate metadata")
        if app["terminates_on_sigterm"] is not True:
            raise ValueError("v1 applications must terminate after SIGTERM")
        for field in ("default_arguments", "environment"):
            if not isinstance(app[field], list):
                raise ValueError(f"{field}: expected an array")
            for entry in app[field]:
                text(entry, field)
                if field == "environment" and ("=" not in entry or entry.startswith("=")):
                    raise ValueError("environment entries require NAME=value")
        validated[app_id] = app
    canonical = json.dumps(registry, sort_keys=True, ensure_ascii=False,
                           separators=(",", ":"), allow_nan=False).encode("utf-8")
    registry_id = hashlib.sha256(canonical).hexdigest()
    shared = ["/* Generated from PlatformRegistry. Do not edit. */",
              "#ifndef PUNOS_PLATFORM_REGISTRY_GENERATED_H",
              "#define PUNOS_PLATFORM_REGISTRY_GENERATED_H",
              '#include <appserv/appserv.h>',
              f'#define PLATFORM_REGISTRY_ID "{registry_id}"',
              f"#define PLATFORM_NATIVE_APP_COUNT {len(launches)}"]
    if launches:
        shared += ["static const NativeAppLaunchSpec platform_native_apps[] = {"]
        for launch in launches:
            shared.append("    { " + string_initializer(launch["application_id"]) + ", " +
                          string_initializer(launch["executable"]) + " },")
        shared += ["};"]
    shared += ["#endif", ""]
    source = ['/* Generated NativeAppSpec catalogue. Do not edit. */',
              '#include "platform_registry.h"']
    for index, launch in enumerate(launches):
        app = validated[launch["application_id"]]
        for field in ("default_arguments", "environment"):
            if app[field]:
                source.append(f"static const String app_{index}_{field}[] = {{")
                source.extend("    " + string_initializer(entry) + "," for entry in app[field])
                source.append("};")
    if launches:
        source.append("static const NativeAppSpec native_apps[] = {")
        for index, launch in enumerate(launches):
            app = validated[launch["application_id"]]
            fields = [string_initializer(app[field]) for field in
                      ("application_id", "display_name", "executable")]
            for field in ("default_arguments", "environment"):
                fields += [f"app_{index}_{field}" if app[field] else "NULL", str(len(app[field]))]
            fields += ["true"]
            source.append("    { " + ", ".join(fields) + " },")
        source.append("};")
    source += ["const AppCatalog app_generated_catalog = {",
               "    { (char *)PLATFORM_REGISTRY_ID, 64 },",
               "    { (char *)PLATFORM_REGISTRY_ID, 64 },",
               "    platform_native_apps, native_apps, PLATFORM_NATIVE_APP_COUNT"
               if launches else "    NULL, NULL, PLATFORM_NATIVE_APP_COUNT",
               "};", ""]
    return "\n".join(shared), "\n".join(source)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--registry", required=True)
    parser.add_argument("--metadata", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    try:
        shared, source = generate(read_json(args.registry), read_json(args.metadata))
    except (ValueError, UnicodeError, OSError) as error:
        parser.exit(1, f"catalogue generation failed: {error}\n")
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    (output / "platform_registry.h").write_text(shared, encoding="utf-8")
    (output / "catalog.c").write_text(source, encoding="utf-8")


if __name__ == "__main__":
    main()
