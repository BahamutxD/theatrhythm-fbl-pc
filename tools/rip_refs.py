"""List REX.W instructions in a flat image (tools/flatten_module.py) that address TARGET rip-relatively.

usage: python tools/rip_refs.py IMAGE.bin TARGET [CODE_END]
Covers mov/lea/cmp/movq-imm forms (48/4c 89, 8b, 8d, 39, 3b, 83, c7 with modrm rm=101), which
is how a global is read or written. 89 = write, 8b = read, 39/3b = compare.
"""
import struct
import sys
from pathlib import Path


def main():
    d = Path(sys.argv[1]).read_bytes()
    target = int(sys.argv[2], 16)
    end = int(sys.argv[3], 16) if len(sys.argv) > 3 else len(d)
    for i in range(0, min(end, len(d)) - 8):
        if d[i] in (0x48, 0x4c) and d[i + 1] in (0x89, 0x8b, 0x8d, 0x39, 0x3b, 0x83, 0xc7) and (d[i + 2] & 0xc7) == 0x05:
            imm = 1 if d[i + 1] == 0x83 else 4 if d[i + 1] == 0xc7 else 0
            if i + 7 + imm + struct.unpack_from('<i', d, i + 3)[0] == target:
                print(f'{i:#x} {d[i:i + 7 + imm].hex()}')


if __name__ == '__main__':
    main()
