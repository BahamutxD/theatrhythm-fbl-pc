from paths import ROOT
import os
import random
import unittest
import zlib
from pathlib import Path
from button_icons import (ASSETS, CLEAR_BLOCK, ICON_BUNDLE, ICON_CRC, TEXTURE_BYTES, build, forge_crc,
                          lz4_block_decompress, read_bundle, write_bundle)

GAME = Path(os.environ['THEATRHYTHM_GAME_DIR']) if os.environ.get('THEATRHYTHM_GAME_DIR') else None


def synthetic_bundle(stream, block_sizes):
    return dict(version=7, player=b'5.x.x', engine=b'2021.2.19f1', flags=0x43, hash=bytes(range(16)),
                blocks=[(size, size, 3) for size in block_sizes],
                nodes=[(0, len(stream) - 64, 4, b'CAB-test'), (len(stream) - 64, 64, 0, b'CAB-test.resS')])


class Lz4Tests(unittest.TestCase):
    def test_literals_and_an_overlapping_match(self):
        # "abc", then a match 3 back for 9 bytes (overlaps itself), then a last literal run
        block = bytes([0x35]) + b'abc' + bytes([3, 0]) + bytes([0x10]) + b'!'
        self.assertEqual(lz4_block_decompress(block, 13), b'abcabcabcabc!')

    def test_long_literal_and_match_lengths(self):
        literal = bytes(range(20))
        # literal length 15+5, match length 15+255+26 (+4 minimum)
        block = bytes([0xff, 5]) + literal + bytes([1, 0, 255, 300 - 19 - 255]) + bytes([0x10]) + b'.'
        self.assertEqual(lz4_block_decompress(block, 20 + 300 + 1), literal + bytes([19]) * 300 + b'.')

    def test_size_mismatch_and_bad_offset_are_errors(self):
        with self.assertRaisesRegex(ValueError, 'expected 4'):
            lz4_block_decompress(bytes([0x30]) + b'abc', 4)
        with self.assertRaisesRegex(ValueError, 'offset'):
            lz4_block_decompress(bytes([0x10]) + b'a' + bytes([5, 0]) + bytes([0x00]), 10)


class CrcTests(unittest.TestCase):
    def test_last_four_bytes_reach_any_crc(self):
        rng = random.Random(1)
        for target in (0, 0xffffffff, ICON_CRC, rng.getrandbits(32)):
            data = bytes(rng.getrandbits(8) for _ in range(1000))
            forged = forge_crc(data, target)
            self.assertEqual(zlib.crc32(forged), target)
            self.assertEqual(forged[:-4], data[:-4])


class BundleTests(unittest.TestCase):
    def test_written_bundle_reads_back(self):
        stream = bytes(random.Random(2).getrandbits(8) for _ in range(300))
        b = synthetic_bundle(stream, [256, 44])
        out = read_bundle(write_bundle(b, stream))
        self.assertEqual(out['stream'], stream)
        self.assertEqual(out['nodes'], b['nodes'])
        self.assertEqual(out['hash'], b['hash'])
        self.assertEqual(out['blocks'], [(256, 256, 0), (44, 44, 0)])
        self.assertEqual(out['flags'], 0x40)

    def test_checked_in_sheets_are_whole_textures(self):
        sheets = sorted(p.stem for p in ASSETS.glob('*.bc7'))
        self.assertEqual(sheets, ['keyboard', 'xbox'])
        for name in sheets:
            self.assertEqual((ASSETS / f'{name}.bc7').stat().st_size, TEXTURE_BYTES)


@unittest.skipUnless(GAME and (GAME / ICON_BUNDLE).is_file(), 'needs the game dump (THEATRHYTHM_GAME_DIR)')
class GameBundleTests(unittest.TestCase):
    def test_replacement_keeps_the_crc_and_carries_the_sheet(self):
        original = (GAME / ICON_BUNDLE).read_bytes()
        texture = (ASSETS / 'xbox.bc7').read_bytes()
        out = read_bundle(build(original, texture))
        before = read_bundle(original)
        self.assertEqual(zlib.crc32(out['stream']), ICON_CRC)
        self.assertEqual(out['nodes'], before['nodes'])
        self.assertEqual(out['stream'][:-TEXTURE_BYTES], before['stream'][:-TEXTURE_BYTES])
        self.assertEqual(out['stream'][-TEXTURE_BYTES:-16], texture[:-16])
        self.assertEqual(out['stream'][-16:-4], CLEAR_BLOCK[:12])

    def test_prepare_writes_every_set_as_it_lands_in_memory(self):
        import subprocess, sys, tempfile
        with tempfile.TemporaryDirectory() as out:
            subprocess.run([sys.executable, str(ROOT / 'scripts' / 'button_icons.py'), str(GAME), '--out', out],
                           check=True, stdout=subprocess.DEVNULL)
            folder = Path(out) / 'button_icons'
            textures = {name: (folder / f'{name}.texture').read_bytes() for name in ('ps4', 'xbox', 'keyboard')}
            self.assertEqual(textures['ps4'], read_bundle((GAME / ICON_BUNDLE).read_bytes())['stream'][-TEXTURE_BYTES:])
            for name in ('xbox', 'keyboard'):
                self.assertEqual(textures[name], read_bundle((folder / f'{name}.bundle').read_bytes())['stream'][-TEXTURE_BYTES:])
            self.assertEqual(len(set(textures.values())), 3)

    def test_other_game_versions_are_refused(self):
        original = bytearray((GAME / ICON_BUNDLE).read_bytes())
        b = read_bundle(bytes(original))
        changed = bytearray(b['stream']); changed[0] ^= 1
        with self.assertRaisesRegex(ValueError, 'another game version'):
            build(write_bundle(b, bytes(changed)), bytes(TEXTURE_BYTES))


if __name__ == '__main__':
    unittest.main()
