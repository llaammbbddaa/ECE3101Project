#!/usr/bin/env python3
"""Capture this gamepad's Linux evdev button presses for a firmware remap."""

import argparse
import json
from pathlib import Path
import struct
import sys


DEFAULT_BUTTONS = ["A", "B", "X", "Y", "L", "R", "Z", "Start", "Extra"]
INPUT_EVENT = struct.Struct("@llHHi")
BTN_GAMEPAD = 0x130
# Accept button slots from both the original and remapped firmware.
CAPTURABLE_HID_INDICES = set(range(12)) - {2}


def capture(device, labels):
    mapping = {}
    used_indices = set()
    for label in labels:
        print(f'Press the "{label}" button.', flush=True)
        pressed_index = None
        while True:
            event = device.read(INPUT_EVENT.size)
            if len(event) != INPUT_EVENT.size:
                raise OSError("The gamepad disconnected during capture.")
            _, _, event_type, code, value = INPUT_EVENT.unpack(event)
            if event_type == 0 and code == 3:  # EV_SYN / SYN_DROPPED
                raise OSError("Linux dropped input events. Run the capture again.")
            if event_type != 1:  # EV_KEY
                continue
            hid_index = code - BTN_GAMEPAD
            if pressed_index is None and value == 1 and hid_index in CAPTURABLE_HID_INDICES:
                if hid_index in used_indices:
                    print(f'Already recorded. Press the "{label}" button.', flush=True)
                    continue
                pressed_index = hid_index
                print(f"Captured HID index {hid_index}. Release the button.", flush=True)
            elif pressed_index == hid_index and value == 0:
                mapping[label] = hid_index
                used_indices.add(hid_index)
                break
    return mapping


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", type=Path, help="Linux evdev gamepad device")
    parser.add_argument("--output", type=Path, default=Path("button_mapping.json"))
    parser.add_argument("labels", nargs="*", help="Button labels in prompt order")
    args = parser.parse_args()
    labels = args.labels or DEFAULT_BUTTONS
    if len(labels) > len(DEFAULT_BUTTONS) or len({label.casefold() for label in labels}) != len(labels):
        parser.error("Provide up to nine distinct button labels.")

    device_path = args.device
    if device_path is None:
        candidates = sorted(Path("/dev/input/by-id").glob("*Pro_Micro*-event-joystick"))
        if len(candidates) != 1:
            parser.error("Connect one Pro Micro gamepad or specify --device /dev/input/eventN.")
        device_path = candidates[0]

    try:
        with device_path.open("rb", buffering=0) as device:
            print(f"Reading {device_path}. Release all buttons before starting.")
            mapping = capture(device, labels)
        args.output.write_text(
            json.dumps({"button_hid_indices": mapping}, indent=2) + "\n",
            encoding="utf-8",
        )
    except KeyboardInterrupt:
        print("\nCapture canceled.", file=sys.stderr)
        return 130
    except OSError as error:
        print(error, file=sys.stderr)
        return 1
    print(f"Saved {args.output}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
