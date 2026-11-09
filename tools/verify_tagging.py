#!/usr/bin/env python3
"""Check tagging code anchors in the supported RoF2 client and graphics DLL."""
import hashlib
import struct
import sys
from pathlib import Path

GAME_HASH = "2a8702ad9f722704f01355c0750be7d6f164a8b9c9128ba0cf286ea32b405b0e"
GRAPHICS_HASH = "ade556b68e98f6f39c5578f70431e11c0b9babcc15b8571eb22cf1e2b654417e"


def read_va(data, va, size):
    pe = struct.unpack_from("<I", data, 0x3c)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    table = pe + 24 + optional_size
    for i in range(count):
        entry = table + i * 40
        rva, raw_size, raw = struct.unpack_from("<III", data, entry + 12)
        offset = va - base - rva
        if 0 <= offset and offset + size <= raw_size:
            return data[raw + offset:raw + offset + size]
    return b""


def main():
    root = Path(sys.argv[1]).expanduser() if len(sys.argv) > 1 else Path.home() / ".wine/drive_c/rof2"
    checks = [
        ("eqgame.exe", GAME_HASH, [
            (0x005996e0, "8b44240450", "GetSpawnByID prologue"),
            (0x008cf790, "51568b74240c", "PrepForDestroyPlayer prologue"),
            (0x0058e2d0, "b8a8260000", "SetNameSpriteState prologue"),
            (0x00519750, "8b4424048981c8050000", "SetGameState prologue and state offset"),
            (0x0051f1a0, "64a100000000", "dsp_chat prologue"),
            (0x0058e2f5, "8b8d1c100000", "PlayerClient actor offset 0x101c"),
            (0x007efa10, "8b4110c3", "UniversalChatProxy channel count at 0x10"),
            (0x007efa6f, "8b490c8b0481", "UniversalChatProxy channel list at 0x0c"),
            (0x006514c5, "8b480c55e892e51900", "native chat UI reads currentChatServerApi at +0x0c"),
        ]),
        ("EQGraphicsDX9.dll", GRAPHICS_HASH, [
            (0x10092540, "8b5424048b81c4a900008991c4a90000c20400", "render callback setter at +0xa9c4"),
            (0x10097729, "8b86c4a900003bc37402ffd0", "render callback call site"),
            (0x1009748e, "8b86080f0000", "CRender device at +0x0f08"),
            (0x10088e00, "8d8fc0160000", "CRender view/projection matrix at +0x16c0"),
            (0x100ae370, "83ec20807c242400", "C2DPrimitiveManager Render whole-instruction prologue"),
            (0x10097815, "5355e8546b0100", "world-to-label boundary calls Render(true, false)"),
            (0x10054560, "8d8114010000c3", "actor graphics-position getter returns +0x114"),
        ]),
    ]
    failures = 0
    for filename, expected_hash, anchors in checks:
        try:
            data = (root / filename).read_bytes()
            actual_hash = hashlib.sha256(data).hexdigest()
            ok = actual_hash == expected_hash
            print(f"[{'PASS' if ok else 'FAIL'}] {filename} SHA256: {actual_hash}")
            failures += not ok
            for va, hex_bytes, label in anchors:
                expected = bytes.fromhex(hex_bytes)
                ok = read_va(data, va, len(expected)) == expected
                print(f"[{'PASS' if ok else 'FAIL'}] 0x{va:08X} {label}")
                failures += not ok
        except (OSError, struct.error) as error:
            print(f"[FAIL] {filename}: {error}")
            failures += 1
    print(f"{failures} failure(s)")
    return int(bool(failures))


if __name__ == "__main__":
    sys.exit(main())
