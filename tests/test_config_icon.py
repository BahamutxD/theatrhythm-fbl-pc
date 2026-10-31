from paths import ROOT
import base64
import importlib.util
import os
import random
import re
import unittest
import zlib
from pathlib import Path
from button_icons import read_bundle
from config_icon import (ASSET, BLOCKS, CLEAR_BLOCK, CONFIG_BUNDLE, CONFIG_CRC, ICON_FROM, PATCH, PATCH_BYTES,
                         TEXTURE_BYTES, atlas_node, build, cbc_decrypt, cbc_encrypt, decrypt, encrypt, original_texture,
                         password, secret_of, tile, untile)

GAME = Path(os.environ['THEATRHYTHM_GAME_DIR']) if os.environ.get('THEATRHYTHM_GAME_DIR') else None


def encoded_password(secret, rng):
    """The inverse of password(): a literal that DecryptKey turns into ab<secret>yz."""
    chars = [ord(c) for c in 'ab' + secret + 'yz']
    words, previous = [], 0x5963
    for k in range(len(chars)):
        value = chars[len(chars) - 1 - k]
        first = rng.getrandbits(16)
        words += [first, previous ^ value ^ first]
        previous = value >> 8 & 0xf
    return base64.b64encode(b''.join(w.to_bytes(2, 'big') for w in words)).decode()


class AesTests(unittest.TestCase):
    def test_fips_197_vector(self):
        key = bytes(range(16))
        plain = bytes.fromhex('00112233445566778899aabbccddeeff')
        cipher = bytes.fromhex('69c4e0d86a7b0430d8cdb78070b4c55a')
        self.assertEqual(cbc_encrypt(key, bytes(16), plain), cipher)
        self.assertEqual(cbc_decrypt(key, bytes(16), cipher), plain)

    def test_cbc_chains_blocks(self):
        rng = random.Random(1)
        key, iv = rng.randbytes(16), rng.randbytes(16)
        plain = bytes(64)  # equal blocks must not encrypt alike
        cipher = cbc_encrypt(key, iv, plain)
        self.assertEqual(len({cipher[i:i + 16] for i in range(0, 64, 16)}), 4)
        self.assertEqual(cbc_decrypt(key, iv, cipher), plain)


class CryptTests(unittest.TestCase):
    def test_password_undoes_the_literal_encoding(self):
        rng = random.Random(2)
        for secret in ('plain-ascii', 'ヴ and ヲ', ''):
            self.assertEqual(password(encoded_password(secret, rng)), secret)

    def test_bundle_round_trip_keeps_the_header(self):
        rng = random.Random(3)
        header = rng.randbytes(32)
        for size in (0, 7, 16, 100):
            plain = b'UnityFS\0' + rng.randbytes(size)
            data = encrypt(plain, header, 'secret')
            self.assertEqual(data[:32], header)
            self.assertEqual(len(data) % 16, 0)
            self.assertEqual(decrypt(data, 'secret'), plain)

    def test_wrong_password_is_refused(self):
        data = encrypt(b'UnityFS\0' + bytes(40), bytes(32), 'secret')
        with self.assertRaisesRegex(ValueError, 'another game version'):
            decrypt(data, 'other')


class TextureTests(unittest.TestCase):
    def test_tile_round_trip(self):
        linear = bytes(random.Random(4).getrandbits(8) for _ in range(TEXTURE_BYTES))
        tiled = tile(linear)
        self.assertEqual(untile(tiled), linear)
        # the first tiled blocks are the first tile's Morton order: (0,0) (1,0) (0,1) (1,1)
        self.assertEqual(tiled[16:32], linear[16:32])
        self.assertEqual(tiled[32:48], linear[BLOCKS * 16:BLOCKS * 16 + 16])

    def test_checked_in_patch_is_whole_blocks(self):
        self.assertEqual(ASSET.stat().st_size, PATCH_BYTES)
        x, y, w, h = PATCH
        self.assertLessEqual(x + w, BLOCKS)
        self.assertLessEqual(y + h, BLOCKS)

    def test_runtime_moves_the_sprite_to_the_patch(self):
        # the Display Settings button shows Logo Usage's sprite moved by PATCH - ICON_FROM blocks
        source = (ROOT / 'src' / 'runtime_display_page.c').read_text(encoding='utf-8')
        move = re.search(r'#define ICON_MOVE_X \(\((\d+)-(\d+)\)\*4\)\s*#define ICON_MOVE_Y \(\((\d+)-(\d+)\)\*4\)', source)
        self.assertIsNotNone(move)
        self.assertEqual(tuple(map(int, move.groups())), (PATCH[0], ICON_FROM[0], PATCH[1], ICON_FROM[1]))


@unittest.skipUnless(GAME and (GAME / CONFIG_BUNDLE).is_file(), 'needs the game dump (THEATRHYTHM_GAME_DIR)')
class GameBundleTests(unittest.TestCase):
    def test_replacement_keeps_the_crc_and_changes_only_the_icon(self):
        original = (GAME / CONFIG_BUNDLE).read_bytes()
        secret = secret_of(GAME)
        patch = ASSET.read_bytes()
        out = build(original, secret, patch)
        self.assertEqual(out[:32], original[:32])
        before, after = read_bundle(decrypt(original, secret)), read_bundle(decrypt(out, secret))
        self.assertEqual(zlib.crc32(after['stream']), CONFIG_CRC)
        self.assertEqual(after['nodes'], before['nodes'])
        offset = atlas_node(before)
        self.assertEqual(after['stream'][:offset], before['stream'][:offset])
        old, new = untile(before['stream'][offset:]), untile(after['stream'][offset:])
        x, y, w, h = PATCH
        changed = {i for i in range(BLOCKS * BLOCKS) if old[i * 16:i * 16 + 16] != new[i * 16:i * 16 + 16]}
        inside = {(y + r) * BLOCKS + x + c for r in range(h) for c in range(w)}
        self.assertEqual(changed - inside, {BLOCKS * BLOCKS - 1})  # the CRC block in the corner
        self.assertEqual(new[-16:-4], CLEAR_BLOCK[:12])
        for r in range(h):
            at = ((y + r) * BLOCKS + x) * 16
            self.assertEqual(new[at:at + w * 16], patch[r * w * 16:(r + 1) * w * 16])

    @unittest.skipUnless(importlib.util.find_spec('texture2ddecoder'), 'needs texture2ddecoder')
    def test_patch_goes_where_nothing_is_drawn(self):
        # the button's sprite is 92x92 around the icon, so 10 blocks around the patch must be clear too
        import texture2ddecoder
        bgra = texture2ddecoder.decode_bc7(untile(original_texture(GAME)), BLOCKS * 4, BLOCKS * 4)
        alpha = bgra[3::4]
        x, y, w, h = PATCH
        margin = 10
        drawn = [(bx, by) for by in range(y - margin, y + h + margin) for bx in range(x - margin, x + w + margin)
                 if any(alpha[(by * 4 + r) * BLOCKS * 4 + bx * 4:(by * 4 + r) * BLOCKS * 4 + bx * 4 + 4] != bytes(4)
                        for r in range(4))]
        self.assertEqual(drawn, [])

    def test_other_game_versions_are_refused(self):
        with self.assertRaisesRegex(ValueError, 'another game version'):
            build((GAME / CONFIG_BUNDLE).read_bytes(), secret_of(GAME) + 'x', ASSET.read_bytes())


if __name__ == '__main__':
    unittest.main()
