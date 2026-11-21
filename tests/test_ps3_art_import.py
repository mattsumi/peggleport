import os
import importlib
from io import BytesIO
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from PIL import Image, UnidentifiedImageError

SCRIPT = Path(__file__).resolve().parents[1] / 'tools' / 'import_ps3_art.py'
sys.path.insert(0, str(SCRIPT.parent))
Pak = importlib.import_module('import_ps3_console_art').Pak


def package(path, files):
    entries = bytearray(32 * len(files))
    payload = bytearray()
    for i, (name, data) in enumerate(files.items()):
        name = name.encode('ascii') + b'\0'
        name_offset = len(entries) + len(payload)
        payload.extend(name)
        offset = len(entries) + len(payload)
        payload.extend(data)
        struct.pack_into('>IIQQII', entries, i * 32, name_offset, len(name), offset, len(data), 3, 0)
    header = bytearray(192)
    nonce = bytes(range(16))
    struct.pack_into('>4sHHIIIIQQQ', header, 0, b'\x7fPKG', 0x8000, 1, 192, 0, 0,
                     len(files), len(header) + len(entries) + len(payload), len(header), len(entries) + len(payload))
    content_id = b'UP0000-NPUA30005_00-P_CONTENT0000000'
    header[48:48 + len(content_id)] = content_id
    header[112:128] = nonce
    aes = Cipher(algorithms.AES(bytes.fromhex('2e7b71d7c9c9a14ea3221f188828b8f8')), modes.CTR(nonce)).encryptor()
    path.write_bytes(header + aes.update(bytes(entries + payload)) + aes.finalize())


class Ps3ArtImport(unittest.TestCase):
    def run_import(self, source, output):
        return subprocess.run([sys.executable, str(SCRIPT), str(source), str(output)], capture_output=True, text=True)

    def test_bad_package_leaves_output_untouched(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, output = root / 'bad.pkg', root / 'presentation'
            source.write_bytes(b'\x7fPKG')
            result = self.run_import(source, output)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('truncated PS3 package', result.stderr)
            self.assertFalse(output.exists())

    def test_rejects_unsupported_image_format(self):
        with BytesIO() as encoded:
            Image.new('RGB', (2, 2), 'white').save(encoded, format='TIFF')
            pak = Pak.__new__(Pak)
            pak.name = 'peggle.pak'
            pak.entries = {'image.jpg': ('image.jpg', encoded.getvalue())}
        with self.assertRaises(UnidentifiedImageError):
            pak.image('image.jpg', (2, 2))

    @unittest.skipUnless(os.environ.get('PP_TEST_PS3_DATA'), 'requires local PS3 artwork')
    def test_package_and_extracted_folder_import_same_art(self):
        data = Path(os.environ['PP_TEST_PS3_DATA'])
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            extracted = root / 'copy' / 'USRDIR' / 'data'
            extracted.mkdir(parents=True)
            files = {p.name: p.read_bytes() for p in data.iterdir() if p.name in ('peggle.pak', 'nights.pak', 'nightstu.pak')}
            self.assertIn('nights.pak', files)
            self.assertIn('peggle.pak', files)
            for name, content in files.items():
                (extracted / name).write_bytes(content)
            pkg = root / 'PS3 copy.pkg'
            package(pkg, {**{'USRDIR/data/' + n: b for n, b in files.items()}, '../ignored.bin': b'ignore'})
            results = []
            for source in (pkg, extracted.parents[1], extracted):
                output = root / f'pack-{len(results)}'
                completed = self.run_import(source, output)
                self.assertEqual(completed.returncode, 0, completed.stderr)
                pngs = {p.relative_to(output): p.read_bytes() for p in output.rglob('*.png')}
                self.assertIn(Path('console/menu/home.png'), pngs)
                self.assertIn(Path('console/lobby/ready_button.png'), pngs)
                self.assertNotEqual(self.run_import(source, output).returncode, 0)
                results.append(pngs)
            self.assertFalse((root / 'ignored.bin').exists())
            self.assertEqual(results[0], results[1])
            self.assertEqual(results[0], results[2])


if __name__ == '__main__':
    unittest.main()
