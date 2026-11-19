import argparse
import hashlib
import json
from pathlib import Path
from shutil import copytree

from import_ps3_console_art import Pak, archive_paths


def build(data, base, output):
    data, base, output = data.resolve(), base.resolve(), output.resolve()
    if output.exists() or any(output.is_relative_to(root) for root in (base, data, Path(__file__).resolve().parents[1])):
        raise ValueError('choose a new staging folder outside the source, base and repository')
    target = Path('console/lobby/ready_button.png')
    manifest = 'ps3-online-art-provenance.json'
    if not (base / 'console/lobby').is_dir() or (base / target).exists() or (base / manifest).exists():
        raise ValueError('base must be an existing lobby presentation without the online overlay')
    pak = Pak(archive_paths(data, ('peggle.pak',))['peggle.pak'])
    image, sources = pak.image('images/party/readybutton.jpg', (272, 64), 'images/party/readybutton_.gif')
    copytree(base, output)
    path = output / target
    image.save(path)
    record = {'size': image.size, 'sources': sources, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    (output / manifest).write_text(json.dumps({target.as_posix(): record}, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Add native PS3 online status artwork to a staged lobby presentation.')
    parser.add_argument('data', type=Path, help='PS3 USRDIR/data containing peggle.pak')
    parser.add_argument('base', type=Path, help='existing presentation with console/lobby')
    parser.add_argument('output', type=Path, help='new presentation folder')
    args = parser.parse_args()
    try:
        build(args.data, args.base, args.output)
    except (OSError, ValueError) as exc:
        parser.exit(1, f'Online artwork not staged: {exc}\n')
