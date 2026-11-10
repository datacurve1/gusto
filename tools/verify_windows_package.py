#!/usr/bin/env python3
"""Audit the distributable PE32 plugin without running it or requiring Wine."""
import struct
import sys
from pathlib import Path


def audit(data):
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    machine, count = struct.unpack_from('<HH', data, pe + 4)
    optional_size = struct.unpack_from('<H', data, pe + 20)[0]
    opt = pe + 24
    assert data[:2] == b'MZ' and data[pe:pe+4] == b'PE\0\0'
    assert machine == 0x14c and struct.unpack_from('<H', data, opt)[0] == 0x10b
    sections = []
    for i in range(count):
        offset = opt + optional_size + i * 40
        virtual_size, rva, raw_size, raw = struct.unpack_from('<IIII', data, offset + 8)
        sections.append((rva, max(virtual_size, raw_size), raw))

    def address(rva):
        for base, size, raw in sections:
            if base <= rva < base + size:
                return raw + rva - base
        raise ValueError(f'Unmapped RVA {rva:x}')

    def string(rva):
        offset = address(rva)
        return data[offset:data.index(b'\0', offset)].decode('ascii')

    imports = []
    offset = address(struct.unpack_from('<I', data, opt + 104)[0])
    while any(data[offset:offset+20]):
        imports.append(string(struct.unpack_from('<I', data, offset + 12)[0]).lower())
        offset += 20
    allowed = {'kernel32.dll', 'user32.dll', 'gdi32.dll', 'bcrypt.dll', 'msvcrt.dll'}
    assert set(imports) <= allowed, f'Non-system runtime dependencies: {set(imports)-allowed}'
    exports = address(struct.unpack_from('<I', data, opt + 96)[0])
    names_count = struct.unpack_from('<I', data, exports + 24)[0]
    names = address(struct.unpack_from('<I', data, exports + 32)[0])
    symbols = {string(struct.unpack_from('<I', data, names + i*4)[0]) for i in range(names_count)}
    assert {'InitializeASI', 'GustoInitializeClient'} <= symbols
    return imports


if __name__ == '__main__':
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('build/GUSTO.dll')
    print('PASS: Windows x86 plugin; standard ASI exports; system DLLs only:', ', '.join(audit(path.read_bytes())))
