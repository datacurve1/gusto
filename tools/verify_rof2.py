#!/usr/bin/env python3

import hashlib
import struct
import sys
from pathlib import Path

EXPECTED_HASH = (
    "2a8702ad9f722704f01355c0750be7d6"
    "f164a8b9c9128ba0cf286ea32b405b0e"
)

EXPECTED_BASE = 0x00400000
EXPECTED_SIZE = 0x012C3000

VERSION_DATE_VA = 0x009DD250
VERSION_TIME_VA = 0x009DD25C

EXPECTED_DATE = b"May 10 2013"
EXPECTED_TIME = b"23:30:08"


def check(condition, description):
    print(f"[{'PASS' if condition else 'FAIL'}] {description}")
    return condition


def main():
    path = Path(sys.argv[1]).expanduser() if len(sys.argv) > 1 else (
        Path.home() / ".wine/drive_c/rof2/eqgame.exe"
    )

    print(f"GUSTO RoF2 Executable Verification")
    print(f"File: {path}")
    print()

    try:
        data = path.read_bytes()
    except OSError as exc:
        print(f"[FAIL] Cannot read executable: {exc}")
        return 1

    results = []

    results.append(check(
        len(data) >= 0x40 and data[:2] == b"MZ",
        "DOS executable signature"
    ))

    if not results[-1]:
        return 1

    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]

    results.append(check(
        pe_offset + 24 <= len(data) and data[pe_offset:pe_offset + 4] == b"PE\0\0",
        "PE signature"
    ))

    if not results[-1]:
        return 1

    machine, sections, timestamp, _, _, optional_size, _ = struct.unpack_from(
        "<HHIIIHH", data, pe_offset + 4
    )

    optional = pe_offset + 24

    if optional + optional_size > len(data):
        print("[FAIL] Optional header exceeds file bounds")
        return 1

    magic = struct.unpack_from("<H", data, optional)[0]

    results.append(check(machine == 0x014C, "Intel 80386 machine type"))
    results.append(check(magic == 0x010B, "PE32 optional header"))

    if magic != 0x010B:
        return 1

    image_base = struct.unpack_from("<I", data, optional + 28)[0]
    image_size = struct.unpack_from("<I", data, optional + 56)[0]

    results.append(check(
        image_base == EXPECTED_BASE,
        f"Preferred image base: 0x{image_base:08X}"
    ))

    results.append(check(
        image_size == EXPECTED_SIZE,
        f"Image size: 0x{image_size:08X}"
    ))

    section_table = optional + optional_size
    sections_info = []

    for i in range(sections):
        offset = section_table + i * 40

        if offset + 40 > len(data):
            print("[FAIL] Section table exceeds file bounds")
            return 1

        name = data[offset:offset + 8].rstrip(b"\0").decode(
            "ascii", errors="replace"
        )

        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
            "<IIII", data, offset + 8
        )

        sections_info.append((
            name, virtual_size, virtual_address, raw_size, raw_offset
        ))

    def read_va_string(va):
        rva = va - EXPECTED_BASE

        for name, virtual_size, virtual_address, raw_size, raw_offset in sections_info:
            if virtual_address <= rva < virtual_address + max(virtual_size, raw_size):
                delta = rva - virtual_address

                if delta >= raw_size:
                    return None

                file_offset = raw_offset + delta

                if file_offset >= len(data):
                    return None

                end = data.find(b"\0", file_offset)
                if end < 0:
                    return None

                return data[file_offset:end]

        return None

    actual_date = read_va_string(VERSION_DATE_VA)
    actual_time = read_va_string(VERSION_TIME_VA)

    results.append(check(
        actual_date == EXPECTED_DATE,
        f"Client date: {actual_date!r}"
    ))

    results.append(check(
        actual_time == EXPECTED_TIME,
        f"Client time: {actual_time!r}"
    ))

    actual_hash = hashlib.sha256(data).hexdigest()

    results.append(check(
        actual_hash == EXPECTED_HASH,
        f"SHA256: {actual_hash}"
    ))

    print()
    print(f"Checks passed: {sum(results)}/{len(results)}")

    if all(results):
        print("RESULT: VERIFIED RoF2 TARGET")
        return 0

    print("RESULT: EXECUTABLE DOES NOT MATCH VERIFIED TARGET")
    return 1


if __name__ == "__main__":
    sys.exit(main())
