#!/usr/bin/env python3
"""Test the Pro Micro gamepad's sticks and buttons through Linux evdev."""

import argparse
from collections import Counter
import fcntl
import math
from pathlib import Path
import select
import struct
import sys
import time

from configure_buttons import BTN_GAMEPAD, DEFAULT_BUTTONS, INPUT_EVENT


EV_SYN, EV_KEY, EV_ABS = 0, 1, 3
SYN_REPORT, SYN_DROPPED = 0, 3
STICKS = (("left", (0, 1)), ("right", (2, 5)))  # ABS_X/Y and ABS_Z/RZ
# Physical labels and HID indices in gamecube.ino. The capture JSON holds
# pre-remap assignments and is not the expected mapping for this sketch.
BUTTON_INDICES = (0, 1, 3, 4, 6, 7, 8, 11, 10)
SECTORS = 12
EDGE_RADIUS = 0.50
MIN_SWEEP = math.radians(345)
MIN_TRAVEL = 0.75
CENTER_RADIUS = 0.12


def read_axes(device, codes):
    axes = []
    for code in codes:
        info = bytearray(24)  # struct input_absinfo
        request = (2 << 30) | (len(info) << 16) | (ord("E") << 8) | (0x40 + code)
        try:
            fcntl.ioctl(device, request, info)
        except OSError as error:
            raise OSError(f"Cannot read ABS axis {code}: {error.strerror}") from error
        value, low, high, _, _, _ = struct.unpack("=6i", info)
        if high <= low:
            raise OSError(f"Invalid range for ABS axis {code}: {low}..{high}")
        axes.append((value, low, high))
    return axes


def events_until(device, deadline):
    while (remaining := deadline - time.monotonic()) > 0:
        if not select.select([device], [], [], remaining)[0]:
            return
        data = device.read(INPUT_EVENT.size)
        if len(data) != INPUT_EVENT.size:
            raise OSError("The controller disconnected or returned a short input event.")
        _, _, kind, code, value = INPUT_EVENT.unpack(data)
        if (kind, code) == (EV_SYN, SYN_DROPPED):
            raise OSError("Linux dropped input events. Run the test again.")
        yield kind, code, value


def stick_frames(device, codes, values, timeout):
    dirty = False
    for kind, code, value in events_until(device, time.monotonic() + timeout):
        if kind == EV_ABS and code in codes:
            values[codes.index(code)] = value
            dirty = True
        if (kind, code) == (EV_SYN, SYN_REPORT) and dirty:
            dirty = False
            yield values


def test_stick(device, name, codes, timeout):
    axes = read_axes(device, codes)
    values = [axis[0] for axis in axes]
    minimum = values.copy()
    maximum = values.copy()

    def normalize(index, value):
        _, low, high = axes[index]
        return 2 * (value - low) / (high - low) - 1

    initial = math.hypot(*(normalize(i, value) for i, value in enumerate(values)))
    print(f"complete a 360 on the {name} stick", flush=True)
    print("Sweep the outer edge slowly in one direction until the release prompt.", flush=True)
    sectors = set()
    most_sectors = 0
    previous_angle = None
    sweep = 0.0
    largest_sweep = 0.0
    complete = False
    moved = False
    for values in stick_frames(device, codes, values, timeout):
        for i, value in enumerate(values):
            minimum[i] = min(minimum[i], value)
            maximum[i] = max(maximum[i], value)
        x, y = (normalize(i, value) for i, value in enumerate(values))
        radius = math.hypot(x, y)
        if radius >= EDGE_RADIUS:
            moved = True
            angle = math.atan2(y, x)
            if previous_angle is not None:
                step = math.remainder(angle - previous_angle, math.tau)
                # A large jump cannot demonstrate movement through the skipped arc.
                if abs(step) <= math.pi / 2:
                    sweep += step
                else:
                    sweep = 0.0
                    sectors.clear()
            sectors.add(int((angle % math.tau) * SECTORS / math.tau) % SECTORS)
            most_sectors = max(most_sectors, len(sectors))
            largest_sweep = max(largest_sweep, abs(sweep))
            if len(sectors) == SECTORS and abs(sweep) >= MIN_SWEEP:
                complete = True
                break
            previous_angle = angle
        else:
            previous_angle = None
            sweep = 0.0
            sectors.clear()

    returned = False
    if complete:
        print("  Rotation recorded. Release the stick now.", flush=True)
        for values in stick_frames(device, codes, values, timeout):
            if math.hypot(*(normalize(i, value) for i, value in enumerate(values))) <= CENTER_RADIUS:
                returned = True
                break

    if not returned:
        # Read the current kernel state in case centering coincided with the deadline.
        values = [axis[0] for axis in read_axes(device, codes)]
        returned = moved and math.hypot(*(normalize(i, value) for i, value in enumerate(values))) <= CENTER_RADIUS

    travel = [(-normalize(i, minimum[i]), normalize(i, maximum[i])) for i in range(2)]
    failures = []
    if not complete:
        failures.append("incomplete rotation")
    if any(min(extents) < MIN_TRAVEL for extents in travel):
        failures.append("limited axis travel")
    if initial > CENTER_RADIUS:
        failures.append("stick was off-center at the start")
    if not returned:
        failures.append("return to center was not observed")
    status = "FAIL (" + ", ".join(failures) + ")" if failures else "PASS"
    print(f"  {status} | sectors {most_sectors}/{SECTORS} | sweep {math.degrees(largest_sweep):.0f}°")
    for i, (axis, (_, low, high), extents) in enumerate(zip(("X", "Y"), axes, travel)):
        print(f"  {axis} observed {minimum[i]}..{maximum[i]} / device {low}..{high}"
              f" | negative/positive travel {max(0, extents[0]):.0%}/{max(0, extents[1]):.0%}")
    x, y = (normalize(i, value) for i, value in enumerate(values))
    print(f"  Final offset from center X {x:+.1%}, Y {y:+.1%}")
    return not failures


def test_button(device, label, expected, timeout):
    print(f'Press and release "{label}" (HID index {expected}).', flush=True)
    presses = Counter()
    pressed = False
    released = False
    for kind, code, value in events_until(device, time.monotonic() + timeout):
        if (kind, code) == (EV_SYN, SYN_REPORT) and released:
            break
        if kind != EV_KEY:
            continue
        index = code - BTN_GAMEPAD
        if value == 1:
            presses[index] += 1
        if index == expected:
            if value == 1:
                pressed = True
            elif value == 0 and pressed:
                released = True
    else:
        if pressed:
            print("  FAIL | expected press seen, no release")
        else:
            print("  FAIL | expected press not seen")
        if presses:
            observed = ", ".join(f"{index} ({count} presses)" for index, count in sorted(presses.items()))
            print(f"  Observed HID indices {observed}")
        return False

    unexpected = sorted(presses.keys() - {expected})
    if unexpected or presses[expected] != 1:
        print(f"  FAIL | expected index pressed {presses[expected]} times | unexpected indices {unexpected}")
        return False
    print("  PASS | press and release received")
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", type=Path, help="Linux evdev gamepad device")
    parser.add_argument("--timeout", type=float, default=12, help="Seconds per prompt (default: 12)")
    args = parser.parse_args()
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("--timeout must be a finite positive number.")
    device_path = args.device
    if device_path is None:
        candidates = sorted(Path("/dev/input/by-id").glob("*Pro_Micro*-event-joystick"))
        if len(candidates) != 1:
            parser.error("Connect one Pro Micro gamepad or specify --device /dev/input/eventN.")
        device_path = candidates[0]

    try:
        with device_path.open("rb", buffering=0) as device:
            print(f"Reading {device_path}. Start with both sticks centered and all buttons released.")
            results = []
            for name, codes in STICKS:
                results.append((f"{name} stick", test_stick(device, name, codes, args.timeout)))
            for label, index in zip(DEFAULT_BUTTONS, BUTTON_INDICES):
                results.append((label, test_button(device, label, index, args.timeout)))
    except KeyboardInterrupt:
        print("\nTest canceled.", file=sys.stderr)
        return 130
    except OSError as error:
        print(f"Test interrupted: {error}", file=sys.stderr)
        return 2

    failed = [label for label, passed in results if not passed]
    print(f"\n{len(results) - len(failed)}/{len(results)} controls passed.")
    if failed:
        print("Failed controls: " + ", ".join(failed))
    return int(bool(failed))


if __name__ == "__main__":
    sys.exit(main())
