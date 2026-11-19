import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from io import BytesIO
from pathlib import Path
from struct import pack

from PIL import Image


def encoded(image, fmt='PNG'):
    stream = BytesIO()
    image.save(stream, format=fmt)
    return stream.getvalue()


def pak(*members):
    directory = b''.join(bytes((0, len(name))) + name.encode('ascii') + pack('<IQ', len(data), 0) for name, data in members)
    return bytes.fromhex('c04ac0ba00000000') + directory + b'\x80\0\0' + b'\0\0'.join(data for _, data in members)


class OnlineArtTest(unittest.TestCase):
    def test_stages_both_native_cells_with_mask_without_mutating_base(self):
        tool = Path(__file__).resolve().parents[1] / 'tools/import_ps3_online_art.py'
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            data, base, out = root / 'data', root / 'base', root / 'staged'
            data.mkdir()
            (base / 'console/lobby').mkdir(parents=True)
            color = Image.new('RGB', (272, 64), (200, 20, 30))
            color.paste((10, 210, 50), (136, 0, 272, 64))
            mask = Image.new('L', color.size, 255)
            mask.putpixel((136, 0), 76)
            members = [('Images/Party/ReadyButton.jpg', encoded(color)), ('Images/Party/ReadyButton_.gif', encoded(mask, 'GIF'))]
            (data / 'peggle.pak').write_bytes(pak(*members))
            cmd = [sys.executable, '-B', str(tool), str(data), str(base), str(out)]
            subprocess.run(cmd, check=True, capture_output=True)
            target = 'console/lobby/ready_button.png'
            with Image.open(out / target) as image:
                self.assertEqual(image.size, (272, 64))
                self.assertEqual(image.getpixel((135, 0)), (200, 20, 30, 255))
                self.assertEqual(image.getpixel((136, 0)), (10, 210, 50, 76))
            record = json.loads((out / 'ps3-online-art-provenance.json').read_text())[target]
            self.assertEqual(record['sha256'], hashlib.sha256((out / target).read_bytes()).hexdigest())
            self.assertEqual([s['sha256'] for s in record['sources']], [hashlib.sha256(raw).hexdigest() for _, raw in members])
            self.assertFalse((base / target).exists())
            self.assertNotEqual(subprocess.run(cmd, capture_output=True).returncode, 0)
