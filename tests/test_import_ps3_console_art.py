import hashlib
import struct
import tempfile
import unittest
from io import BytesIO
from pathlib import Path

from PIL import Image

from tools import import_ps3_console_art as ps3


def pak(*members):
    directory = bytearray(bytes.fromhex('c04ac0ba00000000'))
    payload = bytearray(b'\x80\x02\x00\xaa\xbb')
    for index, (name, data) in enumerate(members):
        raw = name.encode('ascii')
        directory += bytes((0, len(raw))) + raw + struct.pack('<IQ', len(data), 0)
        if index:
            payload += b'\x03\x00\xdd\xee\xff'
        payload += data
    return bytes(directory + payload)


class PakTest(unittest.TestCase):
    def test_reads_case_insensitive_names_and_variable_padding(self):
        data = pak(('Images\\Some.JPG', b'one'), ('Second', b'two'))
        with tempfile.TemporaryDirectory() as scratch:
            source = Path(scratch) / 'Mixed.PAK'
            source.write_bytes(data)
            archive = ps3.Pak(source)
            self.assertEqual(archive.entries['images/some.jpg'], ('Images\\Some.JPG', b'one'))
            self.assertEqual(archive.entries['second'], ('Second', b'two'))
            self.assertEqual(archive.sha256, hashlib.sha256(data).hexdigest())
            self.assertEqual(source.read_bytes(), data)

    def test_rejects_damaged_archive(self):
        data = pak(('First', b'one'), ('Second', b'two'))
        with tempfile.TemporaryDirectory() as scratch:
            source = Path(scratch) / 'Bad.pak'
            for damaged in (b'', data[:10], data[:20], data[:46], data[:-1], data + b'extra',
                            pak(('DUP', b'1'), ('dup', b'2'))):
                with self.subTest(damaged=damaged):
                    source.write_bytes(damaged)
                    with self.assertRaises(ValueError):
                        ps3.Pak(source)


def encoded(image, fmt='PNG'):
    data = BytesIO()
    image.save(data, format=fmt)
    return data.getvalue()


class ArtTest(unittest.TestCase):
    def test_title_shadow_uses_the_mask_as_opacity(self):
        mask = Image.new('RGBA', (3, 1))
        mask.putdata([(0, 0, b, 255) for b in (0, 95, 255)])
        shadow = ps3.menu_variant(mask, 'title_shadow')
        self.assertEqual([shadow.getpixel((x, 0)) for x in range(3)], [(255, 255, 255, a) for a in (0, 95, 255)])

    def test_footer_sleeve_removes_only_unused_atlas_space(self):
        atlas = Image.new('RGBA', (190, 72))
        atlas.putpixel((0, 0), (145, 115, 72, 255))
        atlas.putpixel((154, 71), (53, 38, 93, 128))
        sleeve = ps3.menu_variant(atlas, 'footer_sleeve')
        self.assertEqual(sleeve.size, (155, 72))
        self.assertEqual(sleeve.tobytes(), atlas.crop((0, 0, 155, 72)).tobytes())

    def test_menu_footer_flips_and_locked_button_keeps_its_mask(self):
        footer = Image.new('RGBA', (2, 2), (7, 12, 99, 128))
        footer.putpixel((0, 0), (20, 40, 60, 110))
        self.assertEqual(ps3.menu_variant(footer, 'footer').getpixel((0, 1)), (20, 40, 60, 110))
        source = Image.new('RGBA', (90, 180), (7, 12, 99, 128))
        locked = ps3.menu_variant(source, 'button_locked')
        self.assertEqual(locked.size, (90, 60))
        r, g, b, a = locked.getpixel((45, 42))
        self.assertEqual((r, g, b, a), (r, r, r, 128))

    def test_locked_plate_has_the_light_silver_treatment_of_the_console(self):
        source = Image.new('RGBA', (90, 180), (46, 46, 46, 255))
        source.putpixel((0, 0), (10, 10, 10, 0))
        source.putpixel((45, 20), (125, 125, 125, 255))
        locked = ps3.menu_variant(source, 'button_locked')
        dark, bright = locked.getpixel((45, 36))[0], locked.getpixel((45, 20))[0]
        self.assertGreaterEqual(dark, 155)
        self.assertGreater(bright, dark + 25)
        self.assertEqual(locked.getpixel((0, 0))[3], 0)

    def test_palette_mask_uses_decoded_grayscale_not_indices_or_mask_alpha(self):
        color = Image.new('RGB', (3, 1), (170, 90, 41))
        mask = Image.new('P', (3, 1))
        mask.putpalette([128] * 3 + [255] * 3 + [0] * 3 + [0] * 759)
        mask.putdata([2, 0, 1])
        with tempfile.TemporaryDirectory() as scratch:
            source = Path(scratch) / 'one.pak'
            source.write_bytes(pak(('Art.PNG', encoded(color)), ('Art_.GIF', encoded(mask, 'GIF'))))
            image, layers = ps3.Pak(source).image('art.png', (3, 1), 'art_.gif')
            self.assertEqual([image.getpixel((x, 0)) for x in range(3)], [(170, 90, 41, a) for a in (0, 128, 255)])
            self.assertEqual([layer['member'] for layer in layers], ['Art.PNG', 'Art_.GIF'])

    def test_png_keeps_straight_embedded_alpha(self):
        with tempfile.TemporaryDirectory() as scratch:
            source = Path(scratch) / 'one.pak'
            source.write_bytes(pak(('Night.PNG', encoded(Image.new('RGBA', (1, 1), (181, 91, 55, 75))))))
            image, _ = ps3.Pak(source).image('night.png', (1, 1))
            self.assertEqual(image.getpixel((0, 0)), (181, 91, 55, 75))

    def test_rejects_mismatched_mask(self):
        with tempfile.TemporaryDirectory() as scratch:
            source = Path(scratch) / 'one.pak'
            source.write_bytes(pak(('art.png', encoded(Image.new('RGB', (2, 2)))),
                                   ('mask.gif', encoded(Image.new('L', (2, 3)), 'GIF'))))
            with self.assertRaisesRegex(ValueError, 'mask.gif'):
                ps3.Pak(source).image('art.png', (2, 2), 'mask.gif')


class PackTest(unittest.TestCase):
    def test_refuses_to_write_in_source_tree_or_over_existing_output(self):
        with tempfile.TemporaryDirectory() as scratch:
            parent = Path(scratch)
            source = parent / 'source'
            source.mkdir()
            with self.assertRaisesRegex(ValueError, 'outside'):
                ps3.build(source, source / 'pack')
            output = parent / 'pack'
            output.mkdir()
            old = output / 'keep.txt'
            old.write_bytes(b'keep')
            with self.assertRaises(FileExistsError):
                ps3.build(source, output)
            self.assertEqual(list(output.iterdir()), [old])
            self.assertEqual(old.read_bytes(), b'keep')

    def test_missing_required_member_does_not_create_pack(self):
        with tempfile.TemporaryDirectory() as scratch:
            parent = Path(scratch)
            source = parent / 'source'
            source.mkdir()
            for archive in ('PEGGLE.PAK', 'Nights.pak'):
                (source / archive).write_bytes(pak(('not-art', b'x')))
            with self.assertRaisesRegex(ValueError, 'missing member'):
                ps3.build(source, parent / 'pack')
            self.assertFalse((parent / 'pack').exists())

    def test_gutter_blends_at_authored_offset_and_leaves_opaque_background(self):
        base = Image.new('RGBA', (3, 3), (18, 40, 60, 255))
        layer = Image.new('RGBA', (2, 2), (84, 100, 0, 128))
        output = ps3.compose_gutter(base, layer, (2, 2), (-1, 1))
        self.assertEqual(output.getpixel((0, 1)), (51, 70, 30, 255))
        self.assertEqual(output.getpixel((1, 1)), (18, 40, 60, 255))
        self.assertEqual(output.getpixel((0, 0)), (18, 40, 60, 255))
