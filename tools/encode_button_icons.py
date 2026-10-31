#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Encode a replacement button icon sheet as the game's atlas texture data.

    python tools/encode_button_icons.py assets/button_icons/xbox.png

Writes the .bc7 file next to the PNG: the 512x256 sheet in the layout of the game's
ButtonIcon atlas (sprite boxes in assets/button_icons/sprite_rects.json), stored the way the
PS4 texture is: rows bottom-up, BC7, in the PS4's tiled order (8x8 blocks per tile, Morton order
inside). scripts/button_icons.py puts that data into the game's bundle. Needs Pillow and etcpak
(pip install pillow etcpak); the .bc7 files are checked in, so the game image does not.
"""
import argparse
from pathlib import Path
import sys

import etcpak
from PIL import Image

WIDTH, HEIGHT = 512, 256
BLOCK_BYTES = 16


def morton(t):
    """Block coordinates of the t-th block (0..63) inside an 8x8-block tile."""
    x = y = 0
    for bit in range(3):
        x |= ((t >> (2 * bit)) & 1) << bit
        y |= ((t >> (2 * bit + 1)) & 1) << bit
    return x, y


def tile(linear, blocks_wide, blocks_high):
    """Linear (row-major) BC blocks -> the PS4's tiled order."""
    out = bytearray()
    for ty in range(blocks_high // 8):
        for tx in range(blocks_wide // 8):
            for t in range(64):
                mx, my = morton(t)
                i = ((ty * 8 + my) * blocks_wide + tx * 8 + mx) * BLOCK_BYTES
                out += linear[i:i + BLOCK_BYTES]
    return bytes(out)


def untile(tiled, blocks_wide, blocks_high):
    out = bytearray(len(tiled))
    src = 0
    for ty in range(blocks_high // 8):
        for tx in range(blocks_wide // 8):
            for t in range(64):
                mx, my = morton(t)
                i = ((ty * 8 + my) * blocks_wide + tx * 8 + mx) * BLOCK_BYTES
                out[i:i + BLOCK_BYTES] = tiled[src:src + BLOCK_BYTES]
                src += BLOCK_BYTES
    return bytes(out)


def encode(sheet):
    if sheet.size != (WIDTH, HEIGHT):
        raise SystemExit(f'expected a {WIDTH}x{HEIGHT} sheet, got {sheet.size[0]}x{sheet.size[1]}')
    rgba = sheet.convert('RGBA').transpose(Image.FLIP_TOP_BOTTOM)  # Unity stores rows bottom-up
    # Linear weights keep alpha as exact as colour: perceptual ones left clear pixels between
    # letters up to 45% opaque. Top quality, all partitions: the sheet is small.
    params = etcpak.BC7CompressBlockParams()
    params.init_linear_weights()
    params.m_uber_level = 4
    params.m_max_partitions = 64
    params.m_try_least_squares = True
    linear = etcpak.compress_bc7(rgba.tobytes(), WIDTH, HEIGHT, params)
    return tile(linear, WIDTH // 4, HEIGHT // 4)


def decode(tiled):
    """The sheet as the game will show it (for checking an encode)."""
    import texture2ddecoder  # etcpak's own decoder fails on whole sheets
    linear = untile(tiled, WIDTH // 4, HEIGHT // 4)
    bgra = texture2ddecoder.decode_bc7(linear, WIDTH, HEIGHT)
    return Image.frombytes('RGBA', (WIDTH, HEIGHT), bgra, 'raw', 'BGRA').transpose(Image.FLIP_TOP_BOTTOM)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('sheets', nargs='+', type=Path)
    p.add_argument('--preview', action='store_true', help='also write <name>.decoded.png')
    a = p.parse_args()
    for path in a.sheets:
        data = encode(Image.open(path))
        assert len(data) == WIDTH * HEIGHT, len(data)
        path.with_suffix('.bc7').write_bytes(data)
        if a.preview:
            decode(data).save(path.with_suffix('.decoded.png'))
        print(f'{path.with_suffix(".bc7")}: {len(data)} bytes')
    return 0


if __name__ == '__main__':
    sys.exit(main())
