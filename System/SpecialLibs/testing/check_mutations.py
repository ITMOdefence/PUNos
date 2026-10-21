#!/usr/bin/env python3
"""Verify regressions in a disposable source copy on x86_64 Linux/GCC."""
import argparse
from dataclasses import dataclass
from pathlib import Path
import shutil
import subprocess
import tempfile


@dataclass(frozen=True)
class Mutation:
    name: str
    library: str
    source: str
    changes: tuple
    check: int

    def apply(self, text):
        for old, new in self.changes:
            if text.count(old) != 1:
                raise ValueError(f"{self.name}: expected exactly one source match: {old!r}")
            text = text.replace(old, new, 1)
        return text


IPC = "SpecialLibs/ipclib/src/ipc.c"
INPUT = "SpecialLibs/inputlib/src/input.c"
CAMERA = "SpecialLibs/camlib/src/camera.c"
LOG = "SpecialLibs/loglib/src/log.c"
V4L2 = "SystemLibs/libv4l2/src/v4l2.c"
MUTATIONS = [
    Mutation("IPC overlapping queue shift", "SpecialLibs/ipclib", IPC, (
        ("ErrorCode move_result = memory_move", "ErrorCode move_result = memory_copy"),
        ("if (move_result != CORE_OK)", "if (false && move_result != CORE_OK)")), 117),
    Mutation("IPC lost terminal connect error", "SpecialLibs/ipclib", IPC, (
        ("if (connection->connect_error != 0)", "if (false && connection->connect_error != 0)"),), 95),
    Mutation("input allocation retry", "SpecialLibs/inputlib", INPUT, (
        ("if (device->frame_ready)", "if (false && device->frame_ready)"),), 90),
    Mutation("input overflow resync boundary", "SpecialLibs/inputlib", INPUT, (
        ("device->resyncing = kind != INPUT_EVENT_KIND_SYNC_REPORT;", "device->resyncing = true;"),), 106),
    Mutation("input operations after DeviceLost", "SpecialLibs/inputlib", INPUT, (
        ("if (device->raw_device == NULL || device->device_lost) {\n        return CORE_INVALID_STATE;\n    }\n\n    /* Allocation",
         "if (device->raw_device == NULL) {\n        return CORE_INVALID_STATE;\n    }\n\n    /* Allocation"),), 110),
    Mutation("camera foreign frame identity", "SpecialLibs/camlib", CAMERA, (
        ("if (frame->data.data != issued->data.data || frame->data.size != issued->data.size ||\n        frame->timestamp != issued->timestamp || frame->sequence != issued->sequence)",
         "if (false && (frame->data.data != issued->data.data || frame->data.size != issued->data.size ||\n        frame->timestamp != issued->timestamp || frame->sequence != issued->sequence))"),), 122),
    Mutation("camera stale frame sequence", "SpecialLibs/camlib", CAMERA, (
        ("frame->sequence != issued->sequence", "false"),), 128),
    Mutation("camera list allocation size", "SpecialLibs/camlib", CAMERA, (
        ("core_size_multiply(count, sizeof(CameraStreamConfig), &allocation_size)",
         "core_size_multiply(v4l2_list.count, sizeof(CameraStreamConfig), &allocation_size)"),), 156),
    Mutation("camera query while Streaming", "SpecialLibs/camlib", CAMERA, (
        ("camera->v4l2_device == NULL ||\n        (camera->state != CAMERA_STATE_OPEN && camera->state != CAMERA_STATE_CONFIGURED)", "camera->v4l2_device == NULL"),), 142),
    Mutation("camera fd after DeviceLost", "SpecialLibs/camlib", CAMERA, (
        ("camera->v4l2_device == NULL || camera->state == CAMERA_STATE_DEVICE_LOST", "camera->v4l2_device == NULL"),), 145),
    Mutation("camera rejected close retains handle", "SpecialLibs/camlib", CAMERA, (
        ("if (target->v4l2_device != NULL) {\n            /* A rejected close", "if (false && target->v4l2_device != NULL) {\n            /* A rejected close"),), 151),
    Mutation("log append after failed rotation", "SpecialLibs/loglib", LOG, (
        ("if (store->file == NULL) {\n        return CORE_INVALID_STATE;\n    }\n    if (record->component",
         "if (false && store->file == NULL) {\n        return CORE_INVALID_STATE;\n    }\n    if (record->component"),), 93),
    Mutation("V4L2 capability flag ABI", "SystemLibs/libv4l2", V4L2, (
        ("#define V4L2_CAP_DEVICE_CAPS 0x80000000u", "#define V4L2_CAP_DEVICE_CAPS 0x00000080u"),), 16),
    Mutation("V4L2 bytesused bound", "SystemLibs/libv4l2", V4L2, (
        ("(usize)buf.bytesused > buffers_of(device)[buf.index].length", "false"),), 88),
    Mutation("V4L2 saved QUERYCAP errno", "SystemLibs/libv4l2", V4L2, (
        ("return core_error_from_errno(err);\n        }\n        usable", "return last_error();\n        }\n        usable"),), 91),
    Mutation("extended keyboard key classification", "SystemLibs/libinputdevice",
             "SystemLibs/libinputdevice/src/key_capabilities.h", (
        ("code >= 0x100u && code < 0x160u", "code >= 0x100u"),), 38),
    Mutation("camera stop failure state", "SpecialLibs/camlib", CAMERA, (
        ("camera->state = CAMERA_STATE_DEVICE_LOST;\n        camera->configured_frames = 0;",
         "camera->state = CAMERA_STATE_STREAMING;\n        camera->configured_frames = 0;"),), 166),
    Mutation("input partial frame after read failure", "SpecialLibs/inputlib", INPUT, (
        ("/* The raw layer has not consumed events on failure.",
         "input_discard_all(device);\n                /* The raw layer has not consumed events on failure."),), 116),
    Mutation("IPC NULL payload rejection", "SpecialLibs/ipclib", IPC, (
        ("message->payload.size != 0 && message->payload.data == NULL",
         "false && message->payload.size != 0 && message->payload.data == NULL"),), 126),
    Mutation("log invalid String rejection", "SpecialLibs/loglib", LOG, (
        ("string_c_str(record->component, &component_data) != CORE_OK ||\n        string_c_str(record->message, &message_data) != CORE_OK",
         "false && (string_c_str(record->component, &component_data) != CORE_OK ||\n        string_c_str(record->message, &message_data) != CORE_OK)"),), 106),
    Mutation("V4L2 partial start retry", "SystemLibs/libv4l2", V4L2, (
        ("if (buffers_of(device)[i].queued) { continue; }",
         "if (false && buffers_of(device)[i].queued) { continue; }"),), 96),
    Mutation("V4L2 extra driver buffers", "SystemLibs/libv4l2", V4L2, (
        ("if (reqbufs.count > V4L2_BUFFER_COUNT)",
         "if (false && reqbufs.count > V4L2_BUFFER_COUNT)"),), 96),
    Mutation("V4L2 stale buffer ownership", "SystemLibs/libv4l2", V4L2, (
        ("if (mapped->queued || device->outstanding_count == 0 ||\n        buffer->data.data != issued->data.data || buffer->data.size != issued->data.size ||\n        buffer->timestamp != issued->timestamp || buffer->sequence != issued->sequence)",
         "if (false && (mapped->queued || device->outstanding_count == 0 ||\n        buffer->data.data != issued->data.data || buffer->data.size != issued->data.size ||\n        buffer->timestamp != issued->timestamp || buffer->sequence != issued->sequence))"),), 103),
]


def check(system, library):
    result = subprocess.run(["make", "-C", str(system / library), "check"],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return result.returncode, result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[3])
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="punos-mutations-") as directory:
        system = Path(directory) / "System"
        shutil.copytree(args.source / "System", system,
                        ignore=shutil.ignore_patterns("build", "download", "rootfs", "images", "linux-*"))
        libraries = sorted({mutation.library for mutation in MUTATIONS})
        for library in libraries:
            status, output = check(system, library)
            if status:
                raise RuntimeError(f"Baseline {library} failed:\n{output}")
        for mutation in MUTATIONS:
            source = system / mutation.source
            original = source.read_text(encoding="utf-8")
            try:
                source.write_text(mutation.apply(original), encoding="utf-8")
                status, output = check(system, mutation.library)
                if status == 0 or f"runtime test failed: check {mutation.check}\n" not in output:
                    raise RuntimeError(f"{mutation.name}: expected check {mutation.check}:\n{output}")
                print(f"{mutation.name}: detected by check {mutation.check}", flush=True)
            finally:
                source.write_text(original, encoding="utf-8")
            # Check the restored implementation before attempting the next mutant.
            status, output = check(system, mutation.library)
            if status:
                raise RuntimeError(f"Restored {mutation.library} failed:\n{output}")
    print(f"All {len(MUTATIONS)} mutations detected.")


if __name__ == "__main__":
    main()
