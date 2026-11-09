"""Print flash/RAM usage of the built firmware ELFs as JSON, grouped by section and by origin."""
import json
import os
import subprocess
import sys
from pathlib import Path

LOCAL = Path(os.environ["LOCALAPPDATA"]) / "M5RoverTools"
BIN = LOCAL / "pio-core" / "packages" / "toolchain-xtensa-esp32" / "bin"
BUILD = LOCAL / "pio-build" / "m5rover"

# First matching prefix wins; symbols are demangled with nm -C.
GROUPS = [
    ("M5GFX", ("lgfx", "m5gfx", "fonts::", "lgfx_")),
    ("M5Unified", ("m5::", "M5Unified", "M5Class")),
    ("ControlCore", ("control::",)),
    ("Hardware", ("hardware::",)),
    ("RadioLink", ("RadioLink",)),
    ("Application", ("(anonymous namespace)", "loop", "setup", "canvas", "ui::", "debug::")),
    ("Arduino core", ("TwoWire", "Wire", "Preferences", "WiFi", "HardwareSerial", "Serial", "String", "Print",
                      "Stream", "IPAddress", "esp32-hal", "ESPClass", "SPI")),
]


def tool(name, *args):
    exe = BIN / f"xtensa-esp32-elf-{name}.exe"
    return subprocess.run([str(exe), *args], capture_output=True, text=True, check=True).stdout


def sections(elf):
    result = {}
    for line in tool("size", "-A", str(elf)).splitlines():
        parts = line.split()
        if len(parts) != 3 or not parts[0].startswith("."):
            continue
        name = parts[0]
        if name.startswith((".xt.", ".comment", ".xtensa")):
            continue  # not loaded to the device
        if name.startswith(".phyiram"):
            name = ".phyiram"
        result[name] = result.get(name, 0) + int(parts[1])
    return {name: size for name, size in result.items() if size}


def groups(elf):
    totals = {}
    for line in tool("nm", "-S", "-C", str(elf)).splitlines():
        parts = line.split(None, 3)
        if len(parts) < 4 or len(parts[1]) != 8:
            continue
        try:
            size = int(parts[1], 16)
        except ValueError:
            continue
        kind, name = parts[2], parts[3]
        if kind in "Uu" or size == 0:
            continue
        origin = "ESP-IDF and system"
        for label, prefixes in GROUPS:
            if name.startswith(prefixes) or any(p in name[:60] for p in prefixes if len(p) > 5):
                origin = label
                break
        bucket = "code" if kind in "Tt" else "rodata" if kind in "Rr" else "data" if kind in "DdGgSs" else "bss" if kind in "Bb" else "other"
        totals.setdefault(origin, {}).setdefault(bucket, 0)
        totals[origin][bucket] += size
    return totals


def main():
    report = {}
    for role in ("controller", "rover"):
        elf = BUILD / role / "firmware.elf"
        binary = BUILD / role / "firmware.bin"
        report[role] = {
            "sections": sections(elf),
            "groups": groups(elf),
            "binBytes": binary.stat().st_size,
        }
    json.dump(report, sys.stdout, indent=1)


if __name__ == "__main__":
    main()
