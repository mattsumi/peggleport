import importlib.util
from pathlib import Path
import tempfile
import unittest

import cv2 as cv
import numpy as np
from PIL import Image

spec = importlib.util.spec_from_file_location('import_hd_art', Path(__file__).resolve().parents[1] / 'tools/import_hd_art.py')
assert spec is not None and spec.loader is not None
hd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hd)


class HDArtTest(unittest.TestCase):
    def test_output_must_be_outside_a_checkout(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            checkout = root / 'checkout'
            checkout.mkdir()
            (checkout / '.git').mkdir()
            pc, ipad = root / 'pc', root / 'ipad'
            pc.mkdir()
            (pc / 'art.tsv').write_text('Deluxe\n')
            manifest = ipad / 'en.lproj/properties/1024x768/resources.xml'
            manifest.parent.mkdir(parents=True)
            manifest.write_text('<resources/>')
            for output in (checkout / 'new/pack', checkout):
                with self.subTest(output=output), self.assertRaisesRegex(ValueError, 'checkout'):
                    hd.build(pc, ipad, output)
            self.assertEqual(list(checkout.iterdir()), [checkout / '.git'])

    def test_external_output_is_built_once(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            pc, ipad, output = root / 'pc', root / 'ipad', root / 'pack'
            pc.mkdir()
            manifest = ipad / 'en.lproj/properties/1024x768/resources.xml'
            manifest.parent.mkdir(parents=True)
            manifest.write_text('<resources><image id="IMAGE_TEST" path="sprite"/></resources>')
            (pc / 'art.tsv').write_text('Deluxe\nIMAGE_TEST\tsprite.tga\t1\t1\nBACKGROUND\tscene.tga\t1\t1\n')
            for folder, size, name in ((pc, 8, 'sprite.tga'), (ipad, 16, 'sprite.png')):
                Image.new('RGB', (size, size), 'blue').save(folder / name)
            scenes = ipad / 'levels/1024x768'
            scenes.mkdir(parents=True)
            noise = np.random.default_rng(43).integers(20, 240, (324, 384, 3), dtype=np.uint8)
            scene = Image.fromarray(cv.GaussianBlur(noise, (5, 5), 0))
            scene.save(scenes / 'scene.jpg', quality=96)
            scene.resize((300, 252), Image.Resampling.LANCZOS).save(pc / 'scene.tga')
            hd.build(pc, ipad, output)
            sprite = output / 'sprite.tga'
            with Image.open(sprite) as image:
                self.assertEqual(image.size, (16, 16))
            self.assertTrue((output / 'widescreen/scene.tga').is_file())
            sprite.write_bytes(b'keep')
            with self.assertRaises(FileExistsError):
                hd.build(pc, ipad, output)
            self.assertEqual(sprite.read_bytes(), b'keep')

    def test_atlas_uses_authored_color_and_embedded_alpha(self):
        src = Image.new('RGBA', (24, 24), (96, 48, 24, 128))
        rgba = hd.straight_alpha(src).getpixel((0, 0))
        self.assertEqual(rgba, (191, 96, 48, 128))
        red = Image.new('RGBA', (24, 24), (255, 0, 0, 255))
        blue = Image.new('RGBA', (24, 24), (0, 0, 255, 255))
        sheet = hd.pack_sprites([red, blue], (12, 24), 2, 1)
        self.assertEqual(sheet.size, (24, 48))
        self.assertEqual(sheet.getpixel((0, 23)), (255, 0, 0, 255))
        self.assertEqual(sheet.getpixel((0, 24)), (0, 0, 255, 255))

    def test_registered_backdrop_keeps_stock_detail_where_donor_is_cut_out(self):
        rng = np.random.default_rng(5026)
        art = rng.integers(24, 240, (324, 384, 3), dtype=np.uint8)
        art = cv.GaussianBlur(art, (5, 5), 0)
        for i in range(70):
            x, y = rng.integers([2, 2], [382, 322])
            cv.circle(art, (int(x), int(y)), int(i % 24 + 4), tuple(int(x) for x in rng.integers(20, 250, 3)), -1)
        src = cv.resize(art, (300, 252), interpolation=cv.INTER_AREA)
        art[0:24, 150:235] = 0
        image, matrix, count = hd.register_background(Image.fromarray(src), Image.fromarray(art))
        self.assertGreater(count, 25)
        self.assertAlmostEqual(matrix[0, 0], 300 / 384, delta=0.02)
        ref = np.array(Image.fromarray(src).resize((600, 504), Image.Resampling.LANCZOS))
        out = np.array(image)
        np.testing.assert_array_equal(out[4:25, 260:325], ref[4:25, 260:325])
        self.assertGreater(np.abs(out.astype(float) - ref).mean(), 0.4)
        self.assertEqual(hd.extend_background(image).size, (2880, 504))


if __name__ == '__main__':
    unittest.main()
