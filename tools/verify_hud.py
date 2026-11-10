#!/usr/bin/env python3
"""Read-only audit of native anchors used by the RoF2 HUD preview."""
import hashlib
import struct
import sys
from pathlib import Path
from verify_tagging import GAME_HASH, GRAPHICS_HASH, read_va


def main():
    root = Path(sys.argv[1]).expanduser() if len(sys.argv) > 1 else Path.home() / '.wine/drive_c/rof2'
    checks = [
        ('eqgame.exe', GAME_HASH, [
            (0x51825a, '83fe01750fe8bca53400', 'SetChatMode(1) branches to ClrFocus / normal play'),
            (0x5fda30, '81ec88020000', 'ProcessKeyboardEvents keyboard capture hook'),
            (0x5594e0, '5153558be9', 'KeypressHandler HandleKeyDown'),
            (0x559800, '51535556578bf9', 'KeypressHandler HandleKeyUp'),
            (0x5f9ad0, '538b5c2408', 'native physical-key release helper'),
            (0x4d7230, '64a100000000', 'EQExecuteCmd native command dispatcher'),
            (0x4a9140, '83ec30568bf1', 'EqSoundManager WavePlay native audio getter'),
            (0x539e60, '83ec14a1cc7ce600', 'ProcessMouseEvents DirectInput hook'),
            (0x518870, 'a002f7dd00', 'LMouseDown cursor-hiding entry'),
            (0x518910, '83ec08813d8c7be600dddddddd', 'RMouseDown whole-instruction entry'),
            (0x582020, '83ec085355568bf1', 'CharacterZoneClient Max_Endurance getter'),
            (0x581e60, '568bf18b8640d2ffff', 'CharacterZoneClient Max_Mana getter'),
            (0x57a706, '8d8ec82d0000', 'native Max_Mana caller adjusts this +0x2dc8'),
            (0x7db224, '8b400c', 'ProfileManager next list at +0x0c'),
            (0x52ee40, 'b8c4130000', 'ReportSuccessfulHit whole-instruction prologue'),
            (0x5384d0, '6aff68cf8a9700', 'LMouseUp whole-instruction prologue'),
            (0x537d30, '81ec54030000', 'RMouseUp whole-instruction prologue'),
            (0x868e10, '518b514453555633c057', 'CXWnd IsType runtime types +0x44'),
            (0x875eb0, '8b4164c3', 'CXWndManager GetFocusWnd +0x64'),
            (0x8765f4, 'c786a400000014000000', 'ordinary key-down sets manager mode 20'),
            (0x5f9c90, 'ff1530039c00', 'HandleMouseWheel whole-instruction prologue'),
            (0x9d0de4, struct.pack('<f', 53).hex(), 'native maximum zoom constant'),
        ]),
        ('EQGraphicsDX9.dll', GRAPHICS_HASH, [
            (0x10006470, '558bec83e4f0', 'camera matrix whole-instruction prologue'),
            (0x10005f00, 'd981bc000000', 'camera position getter +0xbc'),
            (0x10005f30, 'd981b0000000', 'camera orientation getter +0xb0'),
        ]),
    ]
    failures = 0
    for name, digest, anchors in checks:
        try:
            data = (root / name).read_bytes()
            ok = hashlib.sha256(data).hexdigest() == digest
            print(f"[{'PASS' if ok else 'FAIL'}] {name} supported SHA256")
            failures += not ok
            for va, hexbytes, label in anchors:
                expected = bytes.fromhex(hexbytes)
                ok = read_va(data, va, len(expected)) == expected
                print(f"[{'PASS' if ok else 'FAIL'}] 0x{va:08X} {label}")
                failures += not ok
        except (OSError, struct.error) as exc:
            print(f'[FAIL] {name}: {exc}')
            failures += 1
    print(f'{failures} failure(s)')
    return int(bool(failures))


if __name__ == '__main__':
    sys.exit(main())
