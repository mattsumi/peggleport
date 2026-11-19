import argparse
import hashlib
import json
from pathlib import Path
from shutil import copytree

from PIL import Image, ImageOps
from import_ps3_console_art import Pak, archive_paths


def build(data, base, output):
    data, base, output = data.resolve(), base.resolve(), output.resolve()
    if output.exists() or output.is_relative_to(data) or output.is_relative_to(base) or output.is_relative_to(Path(__file__).resolve().parents[1]):
        raise ValueError('choose a fresh staging folder outside the source and repository')
    files = archive_paths(data)
    deluxe, nights = Pak(files['peggle.pak']), Pak(files['nights.pak'])
    room = nights.image('images/trophy/trophyroom.png', (1216, 668))
    institute = nights.image('images/mainmenu/mainmenu_nights.jpg', (1216, 668))
    firelight = nights.image('images/mainmenu/firelight.png', (1216, 668))
    action = deluxe.image('images/interface/bigdlgbutton.jpg', (90, 150), 'images/interface/bigdlgbutton_.gif')

    def curtain(name, size):
        prefix = 'images/party/' + name
        return deluxe.image(prefix + '.jpg', size, prefix + '_.gif')

    cloth, cloth_src = curtain('curtain', (602, 668))
    wings, wings_src = curtain('side_curtain', (302, 668))
    top, top_src = curtain('top_curtain', (1186, 211))
    stage = Image.new('RGBA', (1186, 668), (56, 9, 19, 255))
    stage.alpha_composite(cloth, (0, 0))
    stage.alpha_composite(ImageOps.mirror(cloth), (584, 0))
    stage.alpha_composite(wings, (0, 0))
    stage.alpha_composite(ImageOps.mirror(wings), (884, 0))
    stage.alpha_composite(top, (0, 0))
    arts = {'trophy_room': room, 'institute': institute, 'institute_firelight': firelight, 'action_button': action,
            'lobby_stage': (stage, cloth_src + wings_src + top_src),
            'story_deluxe': deluxe.image('images/mainmenu/muralback_wait.jpg', (1186, 668)),
            'story_deluxe_front': deluxe.image('images/mainmenu/mural_wait.jpg', (1186, 668), 'images/mainmenu/mural_wait_.gif'),
            'story_nights': nights.image('images/loading/muralback_wait_night.jpg', (1186, 668)),
            'story_nights_front': nights.image('images/loading/mural_wait_night.png', (1186, 668)),
            'story_panel': deluxe.image('images/dialog/dialogbox.jpg', (246, 249), 'images/dialog/dialogbox_.gif'),
            'story_continue': deluxe.image('images/dialog/playbutton1.jpg', (111, 70)),
            'help_bar': deluxe.image('images/dialog/help_bar.jpg', (1186, 80))}
    copytree(base, output)
    dest = output / 'console' / 'secondary'
    dest.mkdir(parents=True)
    provenance = {}
    for name, (art, sources) in arts.items():
        path = dest / (name + '.png')
        art.save(path)
        provenance[path.relative_to(output).as_posix()] = {'size': art.size, 'sources': sources,
                                                       'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    (output / 'ps3-secondary-art-provenance.json').write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Stage optional Peggle PS3 secondary-screen artwork beside the base presentation assets.')
    parser.add_argument('data', type=Path, help='USRDIR/data containing peggle.pak and nights.pak')
    parser.add_argument('base', type=Path, help='existing base presentation folder')
    parser.add_argument('output', type=Path, help='new presentation folder')
    args = parser.parse_args()
    try:
        build(args.data, args.base, args.output)
    except (ValueError, OSError, Image.DecompressionBombError) as exc:
        parser.exit(1, f'PS3 secondary art was not staged: {exc}\n')
