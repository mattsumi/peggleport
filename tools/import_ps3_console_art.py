import argparse
import hashlib
import json
import struct
from io import BytesIO
from pathlib import Path

from PIL import Image, ImageOps


class Pak:
    def __init__(self, path):
        self.name = path.name
        data = path.read_bytes()
        self.sha256 = hashlib.sha256(data).hexdigest()
        if not data.startswith(bytes.fromhex('c04ac0ba00000000')):
            raise ValueError(f'{self.name}: not a plain Peggle PS3 PAK')

        records = []
        pos = 8
        while pos + 2 <= len(data) and data[pos] == 0:
            name_end = pos + 2 + data[pos + 1]
            if name_end + 12 > len(data) or name_end == pos + 2:
                raise ValueError(f'{self.name}: invalid member directory')
            name = data[pos + 2:name_end].decode('ascii')
            length, = struct.unpack_from('<I', data, name_end)
            records.append((name, length))
            pos = name_end + 12
        if pos + 3 > len(data) or data[pos] != 128 or not records:
            raise ValueError(f'{self.name}: missing directory terminator or empty directory')
        pos += 3 + data[pos + 1]

        self.entries = {}
        for index, (name, length) in enumerate(records):
            key = name.replace('\\', '/').lower()
            end = pos + length
            if end > len(data):
                raise ValueError(f'{self.name}: truncated member {name}')
            if key in self.entries:
                raise ValueError(f'{self.name}: duplicate member {name}')
            self.entries[key] = (name, data[pos:end])
            pos = end
            if index + 1 < len(records):
                if pos + 2 > len(data):
                    raise ValueError(f'{self.name}: missing padding record after {name}')
                pos += 2 + data[pos]
        if pos != len(data):
            raise ValueError(f'{self.name}: length does not match directory')

    def image(self, key, size, mask=None):
        sources = []

        def read(member, mode):
            if member not in self.entries:
                raise ValueError(f'{self.name}: missing member {member}')
            name, data = self.entries[member]
            with Image.open(BytesIO(data)) as image:
                if image.size != size:
                    raise ValueError(f'{self.name}:{name}: expected {size}, got {image.size}')
                decoded = image.convert(mode)
            sources.append({'archive': self.name, 'member': name, 'sha256': hashlib.sha256(data).hexdigest()})
            return decoded

        color = read(key, 'RGBA')
        if mask:
            color.putalpha(read(mask, 'RGB').getchannel('B'))
        return color, sources


def compose_gutter(base, layer, size, offset):
    output = base.copy()
    output.alpha_composite(layer.resize(size, Image.Resampling.BICUBIC), offset)
    return output


def menu_variant(art, name):
    if name == 'title_shadow':
        shadow = Image.new('RGBA', art.size, 'white')
        shadow.putalpha(art.getchannel('B'))
        return shadow
    if name == 'footer':
        return art.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    if name == 'footer_sleeve':
        bounds = art.getchannel('A').getbbox()
        if not bounds:
            raise ValueError('footer sleeve has no visible artwork')
        return art.crop(bounds)
    if name == 'nights_logo':
        row = art.crop((0, 201, 368, 301))
        return row.crop(row.getbbox())
    if name == 'button_locked':
        row = art.crop((0, 0, 90, 60))
        gray = ImageOps.grayscale(row).point([min(249, 120 + i * 85 // 100) for i in range(256)]).convert('RGBA')
        gray.putalpha(row.getchannel('A'))
        return gray
    return art


def archive_paths(source, required=('peggle.pak', 'nights.pak')):
    files = {}
    for path in source.iterdir():
        name = path.name.lower()
        if name in ('peggle.pak', 'nights.pak', 'nightstu.pak'):
            if name in files:
                raise ValueError(f'ambiguous archive name: {name}')
            files[name] = path
    for name in required:
        if name not in files:
            raise ValueError(f'{source}: missing archive {name}')
    return files


def build(source, output):
    source, output = source.resolve(), output.resolve()
    if output.is_relative_to(source) or output.is_relative_to(Path(__file__).resolve().parents[1]):
        raise ValueError('choose a new staging folder outside the source and repository')
    if output.exists():
        raise FileExistsError(f'output already exists: {output}')

    files = archive_paths(source)
    deluxe, nights = Pak(files['peggle.pak']), Pak(files['nights.pak'])

    menu = 'images/mainmenu/'
    loading = 'images/loading/'
    scene = (1186, 668)
    home_size = (1280, 720)
    home, home_sources = deluxe.image(loading + 'loaderback.jpg', home_size)
    hill, hill_sources = deluxe.image(loading + 'loaderhill.jpg', (1280, 400), loading + 'loaderhill_.gif')
    home_front = Image.new('RGBA', home_size)
    home_front.alpha_composite(hill, (0, 320))
    single, single_sources = deluxe.image(menu + 'muralback_sp.jpg', scene)
    single_front, single_front_sources = deluxe.image(menu + 'mural_sp.jpg', scene, menu + 'mural_sp_.gif')
    night, night_sources = nights.image(loading + 'muralback_sp_night.jpg', scene)
    night_overlay, night_overlay_sources = nights.image(loading + 'mural_sp_night.png', scene)
    night_front = Image.alpha_composite(single_front, night_overlay)
    multiplayer, multiplayer_sources = deluxe.image(menu + 'muralback_mp.jpg', home_size)
    multiplayer_front, multiplayer_front_sources = deluxe.image(menu + 'mural_mp.jpg', home_size, menu + 'mural_mp_.gif')
    wait, wait_sources = deluxe.image(menu + 'muralback_wait.jpg', scene)
    wait_front, wait_front_sources = deluxe.image(menu + 'mural_wait.jpg', scene, menu + 'mural_wait_.gif')
    logo, logo_sources = deluxe.image(loading + 'peggleloader.jpg', (426, 212), loading + 'peggleloader_.gif')
    moon, moon_sources = nights.image(loading + 'nightslogo_moon.jpg', (259, 260), loading + 'nightslogo_moon_.gif')
    assets = {
        'console/menu/home.png': (home, home_sources, {}),
        'console/menu/home_front.png': (home_front, hill_sources, {'offset': [0, 320]}),
        'console/menu/single.png': (single, single_sources, {}),
        'console/menu/single_front.png': (single_front, single_front_sources, {}),
        'console/menu/nights.png': (night, night_sources, {}),
        'console/menu/nights_front.png': (night_front, single_front_sources + night_overlay_sources,
                                         {'composition': 'night overlay over the shared Bjorn foreground'}),
        'console/menu/multiplayer.png': (multiplayer, multiplayer_sources, {}),
        'console/menu/multiplayer_front.png': (multiplayer_front, multiplayer_front_sources, {}),
        'console/menu/wait.png': (wait, wait_sources, {}),
        'console/menu/wait_front.png': (wait_front, wait_front_sources, {}),
        'console/menu/logo.png': (logo, logo_sources, {}),
        'console/menu/moon.png': (moon, moon_sources, {}),
    }
    for name, key, size, mask in (
        ('buttons', 'images/interface/bigmenubutton.jpg', (90, 180), 'images/interface/bigmenubutton_.gif'),
        ('button_locked', 'images/interface/bigmenubutton.jpg', (90, 180), 'images/interface/bigmenubutton_.gif'),
        ('button_glow', 'images/interface/bigmenuglow.jpg', (147, 118), None),
        ('lock', 'images/interface/padlock.jpg', (116, 92), 'images/interface/padlock_.gif'),
        ('header', 'images/ip/titlebar.jpg', (500, 180), 'images/ip/titlebar_.gif'),
        ('header_cap', 'images/ip/titlelc.jpg', (166, 97), 'images/ip/titlelc_.gif'),
        ('header_rail', 'images/ip/titleslider.jpg', (470, 61), 'images/ip/titleslider_.gif'),
        ('footer', 'images/ip/titleslider.jpg', (470, 61), 'images/ip/titleslider_.gif'),
        ('footer_rail', 'images/dialog/help_bar.jpg', (1186, 80), None),
        ('footer_sleeve', 'images/dialog/help_slider.jpg', (190, 72), 'images/dialog/help_slider_.gif'),
        ('pad_glyphs', 'images/ps3/26pixbuttons.png', (100, 25), None),
        ('speech', menu + 'speechbubble.jpg', (258, 134), menu + 'speechbubble_.gif'),
        ('sun', menu + 'blanksun.gif', (149, 149), menu + 'blanksun_.gif'),
        ('sun_glow', 'images/sunglow.jpg', (800, 800), 'images/sunglow_.jpg'),
        ('title_sun', loading + 'sunhead.jpg', (150, 150), loading + 'sunhead_.gif'),
        ('title_shadow', loading + 'shadow_.jpg', (460, 236), None),
        ('title_plate', loading + 'loaderoverlay.jpg', (400, 100), loading + 'loaderoverlay_.gif'),
        ('title_buttons', loading + 'playnow.jpg', (344, 100), loading + 'playnow_.gif'),
        ('loader_lights', loading + 'loaderlights.jpg', (90, 49), loading + 'loaderlights_.gif'),
    ):
        art, sources = deluxe.image(key, size, mask)
        assets[f'console/menu/{name}.png'] = (menu_variant(art, name), sources, {})
    archives = [deluxe, nights]
    if 'nightstu.pak' in files:
        update = Pak(files['nightstu.pak'])
        art, sources = update.image('images/game/nights_button.jpg', (368, 402), 'images/game/nights_button_.gif')
        assets['console/menu/nights_logo.png'] = (menu_variant(art, 'nights_logo'), sources, {'source_row': 2})
        archives.append(update)

    gutter = 'images/ip/gutter/bjorn/'
    for side, layer, native, scaled, offset, trim_width, trim_x in (
        ('left', 'hill', (308, 516), (333, 557), (135, 162), 237, 70),
        ('right', 'tree', (249, 570), (269, 616), (-109, 105), 241, 988),
    ):
        base, base_sources = deluxe.image(gutter + side + ' bg.jpg', (296, 720))
        detail, detail_sources = deluxe.image(gutter + layer + '.jpg', native, gutter + layer + '_.gif')
        assets[f'console/gutters/default_{side}.png'] = (
            compose_gutter(base, detail, scaled, offset), base_sources + detail_sources,
            {'foreground_size': scaled, 'foreground_offset': offset,
             'crop_at_1280x720': [136, 0, 296, 720] if side == 'left' else [0, 0, 160, 720]},
        )
        trim, trim_sources = deluxe.image(f'images/ip/bjorn/{side}.jpg', (trim_width, 689),
                                         f'images/ip/common/{side}_.gif')
        assets[f'console/trim/{side}.png'] = (
            trim, trim_sources, {'reference_1280x720_offset': [trim_x, -11], 'reference_scale': 1.08},
        )
    for part, size, x, y in (('top', (613, 108), 327, -12), ('bottom', (613, 63), 327, 666)):
        art, art_sources = deluxe.image(f'images/ip/bjorn/{part}.jpg', size, f'images/ip/bjorn/{part}_.gif')
        assets[f'console/trim/{part}.png'] = (
            art, art_sources, {'reference_1280x720_offset': [x, y], 'reference_scale': 1.08},
        )
    for name, key, size, mask in (
        ('ballotron', 'images/ip/ballotron.jpg', (100, 538), 'images/ip/ballotron_.gif'),
        ('fevermeter', 'images/ip/fevermeter.jpg', (100, 538), 'images/ip/fevermeter_.gif'),
        ('ball_back', 'images/interface/ballback.gif', (28, 400), None),
        ('ball_glass', 'images/interface/rglass.jpg', (70, 487), 'images/interface/rglass_.gif'),
        ('replay_spin', 'images/dialog/replayspin.jpg', (100, 100), 'images/dialog/replayspin_.gif'),
        ('replay_button', 'images/dialog/replaybutton.jpg', (51, 102), 'images/dialog/replaybutton_.gif'),
    ):
        art, art_sources = deluxe.image(key, size, mask)
        assets[f'console/trim/{name}.png'] = (art, art_sources, {})

    provenance = {
        'source_title_id': 'NPUA30005',
        'archives': {pak.name: {'sha256': pak.sha256, 'entries': len(pak.entries)} for pak in archives},
        'reference_boards': [
            {'edition': 'Deluxe', 'stage': '1-1', 'title': 'Peggleland', 'pc_background_stem': 'level1',
             'seconds': 38.5},
            {'edition': 'Nights', 'stage': '1-1', 'title': 'Crimewave', 'pc_background_stem': 'bjorn1',
             'seconds': 240},
        ],
        'notes': [
            'All image pixels come from PAK members. Footage was used only to identify artwork and estimate placement.',
            'Both recorded boards show the Bjorn wings; the static default excludes animated clouds.',
            'Blue side bezels are separate, full masked pieces, not already cropped for a PC HUD.',
            'Marina has separate themed assets in nights.pak, but they do not match either recorded board.',
            'Bjorn is baked into the single-player foreground; no standalone full-body sprite or full-width gameplay backing was identified.',
        ],
        'assets': {},
    }
    output.mkdir(parents=True)
    for relative, (image, sources, transform) in sorted(assets.items()):
        path = output / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        image.save(path)
        provenance['assets'][relative] = {
            'size': image.size, 'sources': sources, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(), **transform,
        }
    (output / 'ps3-art-provenance.json').write_bytes((json.dumps(provenance, indent=2) + '\n').encode('utf-8'))
    return len(assets)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Import native presentation artwork from a local Peggle PS3 dump (Pillow required).')
    parser.add_argument('data', type=Path, help='USRDIR/data containing peggle.pak and nights.pak')
    parser.add_argument('output', type=Path, help='new presentation folder beside the PegglePort executable')
    args = parser.parse_args()
    try:
        count = build(args.data, args.output)
    except (ValueError, OSError, Image.DecompressionBombError) as exc:
        parser.exit(1, f'PS3 art was not imported: {exc}\n')
    print(f'Staged {count} images in {args.output}')
