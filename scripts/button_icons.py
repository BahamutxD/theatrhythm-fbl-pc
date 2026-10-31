#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the game's button icon bundle with Xbox or keyboard icons in place of the PS4 ones.

    python scripts/button_icons.py GAME --out OUT

The game draws every button prompt from one sprite atlas, a 512x256 BC7 texture in
Media/StreamingAssets/aa/PS4/<ICON_BUNDLE>. For each assets/button_icons/<set>.bc7 (encoded by
tools/encode_button_icons.py) this writes OUT/button_icons/<set>.bundle: the game's bundle with
that texture data swapped in, blocks stored uncompressed. Addressables loads the bundle with the
CRC32 of its decompressed data from the catalog, so the last 4 bytes of the texture (the index bits
of a fully transparent block in a corner no sprite uses) are chosen to keep that CRC. The runtime
mounts the bundle over the game's file when settings.ini asks for that set (button_icons).

OUT/button_icons/<set>.texture (ps4 for the game's own) holds each set's texture data exactly as it
lands in guest memory: with button_icons=auto the runtime finds the loaded atlas by it and writes
another set over it when the player changes device (src/runtime_button_icons.c).

No third-party modules or Python dependencies.
"""
import argparse
from pathlib import Path
import struct
import sys
import zlib

PORT = Path(__file__).resolve().parent.parent
ASSETS = PORT / 'assets' / 'button_icons'
ICON_BUNDLE = 'Media/StreamingAssets/aa/PS4/26f8caa7ece23b9c50dba3f9dd2af58f.bundle'
ICON_CRC = 4073553655  # catalog.json m_Crc of ICON_BUNDLE (CUSA32853 v01.00)
TEXTURE_BYTES = 512 * 256  # BC7, one byte per texel, no mipmaps
# The last 16 bytes of the tiled texture are the block at texels x 508-511 of the sheet's top rows
# (stored bottom-up); the nearest sprite ends at x 490. BC7 mode 6 with every endpoint 0 decodes to
# transparent black whatever its index bits, so bytes 12-15 are free for the CRC.
CLEAR_BLOCK = bytes([0x40]) + bytes(15)


def lz4_block_decompress(src, size):
    """One raw LZ4 block (no frame), as Unity stores bundle blocks."""
    out = bytearray()
    i = 0
    while i < len(src):
        token = src[i]; i += 1
        n = token >> 4
        if n == 15:
            while True:
                b = src[i]; i += 1; n += b
                if b != 255: break
        out += src[i:i + n]; i += n
        if i >= len(src): break
        offset = src[i] | src[i + 1] << 8; i += 2
        if not offset or offset > len(out): raise ValueError('bad LZ4 match offset')
        n = token & 15
        if n == 15:
            while True:
                b = src[i]; i += 1; n += b
                if b != 255: break
        n += 4
        start = len(out) - offset
        for k in range(n):  # matches may overlap their own output
            out.append(out[start + k])
    if len(out) != size: raise ValueError(f'LZ4 block gave {len(out)} bytes, expected {size}')
    return bytes(out)


def decompress(data, size, flags):
    kind = flags & 0x3f
    if kind == 0: return data
    if kind in (2, 3): return lz4_block_decompress(data, size)
    raise ValueError(f'unsupported bundle compression {kind}')


def cstring(data, offset):
    end = data.index(b'\0', offset)
    return data[offset:end], end + 1


def read_bundle(data):
    """UnityFS header, directory and the decompressed data stream."""
    signature, o = cstring(data, 0)
    if signature != b'UnityFS': raise ValueError('not a UnityFS bundle')
    version, = struct.unpack_from('>I', data, o); o += 4
    player, o = cstring(data, o)
    engine, o = cstring(data, o)
    _, info_packed, info_size, flags = struct.unpack_from('>qIII', data, o); o += 20
    if version >= 7: o = (o + 15) & ~15
    if flags & 0x80:  # directory at the end of the file
        info = data[len(data) - info_packed:]
        start = o
    else:
        info = data[o:o + info_packed]
        start = o + info_packed
    if flags & 0x200: start = (start + 15) & ~15
    info = decompress(info, info_size, flags)
    p = 16  # uncompressed data hash
    count, = struct.unpack_from('>i', info, p); p += 4
    blocks = []
    for _ in range(count):
        blocks.append(struct.unpack_from('>IIH', info, p)); p += 10
    count, = struct.unpack_from('>i', info, p); p += 4
    nodes = []
    for _ in range(count):
        offset, size, node_flags = struct.unpack_from('>qqI', info, p); p += 20
        name, p = cstring(info, p)
        nodes.append((offset, size, node_flags, name))
    stream = bytearray()
    for size, packed, block_flags in blocks:
        stream += decompress(data[start:start + packed], size, block_flags)
        start += packed
    return dict(version=version, player=player, engine=engine, flags=flags, hash=info[:16],
                blocks=blocks, nodes=nodes, stream=bytes(stream))


def write_bundle(b, stream):
    """The same bundle with its data stream in uncompressed blocks of the original sizes."""
    info = bytearray(b['hash'])
    sizes = [size for size, _, _ in b['blocks']]
    if sum(sizes) != len(stream): raise ValueError('stream size changed')
    info += struct.pack('>i', len(sizes))
    for size in sizes: info += struct.pack('>IIH', size, size, 0)
    info += struct.pack('>i', len(b['nodes']))
    for offset, size, node_flags, name in b['nodes']:
        info += struct.pack('>qqI', offset, size, node_flags) + name + b'\0'
    head = b'UnityFS\0' + struct.pack('>I', b['version']) + b['player'] + b'\0' + b['engine'] + b'\0'
    flags = 0x40  # directory follows the header, nothing compressed
    fixed = len(head) + 20
    pad = ((fixed + 15) & ~15) - fixed if b['version'] >= 7 else 0
    total = fixed + pad + len(info) + len(stream)
    return head + struct.pack('>qIII', total, len(info), len(info), flags) + bytes(pad) + bytes(info) + stream


def forge_crc(data, target):
    """data with its last 4 bytes replaced so that zlib.crc32(data) == target.

    CRC32 is affine in those bytes: solve crc(base) ^ crc(x) ^ crc(0) == target for x over GF(2)."""
    head = data[:-4]
    state = zlib.crc32(head)
    zero = zlib.crc32(bytes(4), state)
    columns = [zlib.crc32((1 << bit).to_bytes(4, 'little'), state) ^ zero for bit in range(32)]
    want = target ^ zero
    # Gaussian elimination: rows are (column value, input bit mask)
    rows = [(columns[bit], 1 << bit) for bit in range(32)]
    basis = {}
    for value, mask in rows:
        for pivot in sorted(basis, reverse=True):
            if value >> pivot & 1:
                value ^= basis[pivot][0]; mask ^= basis[pivot][1]
        if value:
            basis[value.bit_length() - 1] = (value, mask)
    x = 0
    for pivot in sorted(basis, reverse=True):
        if want >> pivot & 1:
            want ^= basis[pivot][0]; x ^= basis[pivot][1]
    if want: raise ValueError('CRC not reachable through the last 4 bytes')
    result = head + x.to_bytes(4, 'little')
    assert zlib.crc32(result) == target
    return result


def original_texture(original):
    """The game's own atlas data (the last TEXTURE_BYTES of the bundle's data stream)."""
    return read_bundle(original)['stream'][-TEXTURE_BYTES:]


def build(original, texture):
    """The icon bundle (bytes) with `texture` as its atlas data, CRC kept."""
    b = read_bundle(original)
    crc = zlib.crc32(b['stream'])
    if crc != ICON_CRC:
        raise ValueError(f'icon bundle CRC {crc} is not {ICON_CRC}: another game version?')
    textures = [n for n in b['nodes'] if n[3].endswith(b'.resS') and n[1] == TEXTURE_BYTES]
    if len(textures) != 1: raise ValueError('no single atlas texture stream in the icon bundle')
    if len(texture) != TEXTURE_BYTES: raise ValueError(f'texture is {len(texture)} bytes, expected {TEXTURE_BYTES}')
    offset = textures[0][0]
    if offset + TEXTURE_BYTES != len(b['stream']):
        raise ValueError('the atlas texture is not at the end of the data stream')
    # forge_crc rewrites the stream's last 4 bytes: the index bits of CLEAR_BLOCK
    stream = b['stream'][:offset] + texture[:-16] + CLEAR_BLOCK
    return write_bundle(b, forge_crc(stream, crc))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('game', type=Path)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    folder = a.out / 'button_icons'
    folder.mkdir(parents=True, exist_ok=True)  # run.py: prepared, even with nothing to build
    source = a.game / ICON_BUNDLE
    if not source.is_file():
        print(f'Button icons: {source} not found, keeping the PS4 icons')
        return 0
    original = source.read_bytes()
    try:
        (folder / 'ps4.texture').write_bytes(original_texture(original))
    except ValueError as e:
        print(f'Button icons: {source}: {e}; keeping the PS4 icons')
        return 0
    for texture in sorted(ASSETS.glob('*.bc7')):
        try:
            bundle = build(original, texture.read_bytes())
        except ValueError as e:
            print(f'Button icons: {texture.stem}: {e}; keeping the PS4 icons')
            continue
        target = folder / f'{texture.stem}.bundle'
        target.write_bytes(bundle)
        (folder / f'{texture.stem}.texture').write_bytes(original_texture(bundle))
        print(f'Button icons: {target} ({len(bundle)} bytes)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
