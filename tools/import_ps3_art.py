import argparse
import os
import struct
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import import_ps3_console_art as console
import import_ps3_lobby_art as lobby
import import_ps3_online_art as online
import import_ps3_secondary_art as secondary

PAKS = ('peggle.pak', 'nights.pak', 'nightstu.pak')


def find_data(source):
    dirs = (source, source / 'data', source / 'USRDIR' / 'data',
            source / 'NPUA30005' / 'USRDIR' / 'data')
    for directory in dirs:
        if directory.is_dir():
            try:
                files = console.archive_paths(directory)
            except ValueError:
                continue
            if all(path.is_file() for path in files.values()):
                return directory
    raise ValueError('choose an extracted Peggle folder containing USRDIR/data/peggle.pak and nights.pak')


def extract_data(source, data):
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

    with source.open('rb') as f:
        header = f.read(192)
        if len(header) != 192:
            raise ValueError('truncated PS3 package header')
        magic, release, platform = struct.unpack_from('>4sHH', header)
        if (magic, release, platform) != (b'\x7fPKG', 0x8000, 1):
            raise ValueError('expected a retail PS3 .pkg')
        count, total, start, size = struct.unpack_from('>IQQQ', header, 20)
        table_size = count * 32
        if not (0 < count <= 65536 and total == os.fstat(f.fileno()).st_size
                and 192 <= start <= total and table_size <= size <= total - start):
            raise ValueError('invalid PS3 package directory or payload size')
        nonce = int.from_bytes(header[112:128], 'big')
        key = algorithms.AES(bytes.fromhex('2e7b71d7c9c9a14ea3221f188828b8f8'))

        def read(offset, length):
            if not 0 <= offset <= size or not 0 <= length <= size - offset:
                raise ValueError('PS3 package entry exceeds the payload')
            block, skip = divmod(offset, 16)
            counter = ((nonce + block) % (1 << 128)).to_bytes(16, 'big')
            decrypt = Cipher(key, modes.CTR(counter)).decryptor()
            f.seek(start + offset - skip)
            raw = f.read(length + skip)
            if len(raw) != length + skip:
                raise ValueError('truncated PS3 package entry')
            return decrypt.update(raw)[skip:]

        expected = {f'{prefix}usrdir/data/{name}'.encode(): name
                    for prefix in ('', 'npua30005/') for name in PAKS}
        files = {}
        for name_offset, name_size, offset, length, kind, _ in struct.iter_unpack('>IIQQII', read(0, table_size)):
            if not 0 < name_size <= 256 or name_offset < table_size or kind & 0x10000000 or kind & 0xff != 3:
                continue
            name = expected.get(read(name_offset, name_size).rstrip(b'\0').lower())
            if not name:
                continue
            if name in files or not table_size <= offset <= size or not 0 < length <= min(64 << 20, size - offset):
                raise ValueError(f'invalid or duplicate archive {name} in PS3 package')
            files[name] = (offset, length)
        if not all(name in files for name in PAKS[:2]):
            raise ValueError('PS3 package does not contain both Peggle artwork archives; try your extracted copy')
        data.mkdir()
        for name, (offset, length) in files.items():
            with (data / name).open('xb') as out:
                while length:
                    amount = min(length, 1 << 20)
                    out.write(read(offset, amount))
                    offset += amount
                    length -= amount
    return data


def build(source, output):
    source, output = source.resolve(), output.absolute()
    if output.exists() or output.is_symlink():
        raise FileExistsError('output already exists; move the old presentation folder first')
    if output.resolve().is_relative_to(source):
        raise ValueError('choose an output folder outside the original game files')
    output.parent.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(prefix='.peggle-ps3-', dir=output.parent) as temp:
        temp = Path(temp)
        if source.is_dir():
            data = find_data(source)
        elif source.is_file() and source.suffix.lower() == '.pkg':
            data = extract_data(source, temp / 'pak')
        else:
            raise ValueError('choose your Peggle PS3 .pkg or a decrypted, extracted folder')
        export, menus, rooms, art = (temp / name for name in ('base', 'menus', 'rooms', 'art'))
        console.build(data, export)
        secondary.build(data, export, menus)
        lobby.build(data, menus, rooms)
        online.build(data, rooms, art)
        if output.exists() or output.is_symlink():
            raise FileExistsError('output already exists; move the old presentation folder first')
        art.rename(output)


def main():
    parser = argparse.ArgumentParser(description='Import artwork from your own Peggle PS3 package or extracted folder.')
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    try:
        build(args.source, args.output)
    except ModuleNotFoundError as exc:
        parser.exit(1, f'Install the import dependency {exc.name} before using this source.\n')
    except (OSError, ValueError, Image.DecompressionBombError) as exc:
        parser.exit(1, f'{exc}\n')


if __name__ == '__main__':
    main()
