import argparse
import hashlib
import json
from pathlib import Path
from shutil import copytree

from PIL import Image, ImageOps

from import_ps3_console_art import Pak, archive_paths


def build(data, base, output):
    data, base, output = data.resolve(), base.resolve(), output.resolve()
    if output.exists() or any(output.is_relative_to(p) for p in (data, base, Path(__file__).resolve().parents[1])):
        raise ValueError('choose a fresh staging folder outside the source and repository')
    if not (base / 'console').is_dir():
        raise ValueError('base must contain a console presentation folder')
    pak = Pak(archive_paths(data, ('peggle.pak',))['peggle.pak'])

    def masked(member, size):
        return pak.image(member + '.jpg', size, member + '_.gif')

    cloth, cloth_src = masked('images/party/curtain', (602, 668))
    wings, wings_src = masked('images/party/side_curtain', (302, 668))
    top, top_src = masked('images/party/top_curtain', (1186, 211))
    stage = Image.new('RGBA', (1186, 668), (0, 0, 0, 255))
    stage.alpha_composite(cloth, (-3, 0))
    stage.alpha_composite(ImageOps.mirror(cloth), (601, 0))
    stage.alpha_composite(wings, (0, 0))
    stage.alpha_composite(ImageOps.mirror(wings), (884, 0))
    stage.alpha_composite(top, (0, 0))
    assets = {
        'stage': (stage, cloth_src + wings_src + top_src),
        'marque': masked('images/party/marque', (800, 216)),
        'party_title': masked('images/party/lobby_header', (400, 150)),
        'duel_title': masked('images/party/duel_header', (400, 150)),
        'sidebar': masked('images/party/sidebar', (320, 720)),
        'party_seats': masked('images/party/lobby_player', (566, 252)),
        'duel_seats': masked('images/duel/duel_player', (566, 360)),
        'host_badge': masked('images/party/hostbadge', (64, 64)),
        'rail': pak.image('images/dialog/help_bar.jpg', (1186, 80)),
    }
    if (base / 'console/lobby').exists() or (base / 'ps3-lobby-art-provenance.json').exists():
        raise ValueError('base already contains a lobby presentation')
    copytree(base, output)
    dest = output / 'console/lobby'
    dest.mkdir()
    provenance = {}
    for name, (image, sources) in assets.items():
        path = dest / (name + '.png')
        image.save(path)
        provenance[path.relative_to(output).as_posix()] = {
            'size': image.size, 'sources': sources, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
        }
    provenance['console/lobby/stage.png']['composition'] = 'Black backing; curtain x=-3, mirrored at x=601; side curtain x=0, mirrored at x=884; top curtain at (0, 0).'
    (output / 'ps3-lobby-art-provenance.json').write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Stage local PS3 lobby artwork over an existing console presentation.')
    parser.add_argument('data', type=Path, help='PS3 USRDIR/data containing peggle.pak')
    parser.add_argument('base', type=Path, help='existing console presentation folder')
    parser.add_argument('output', type=Path, help='new presentation folder')
    args = parser.parse_args()
    try:
        build(args.data, args.base, args.output)
    except (ValueError, OSError, Image.DecompressionBombError) as exc:
        parser.exit(1, f'PS3 lobby art was not staged: {exc}\n')
