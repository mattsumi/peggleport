import argparse
from concurrent.futures import ThreadPoolExecutor
from functools import lru_cache
import json
from pathlib import Path, PurePosixPath
from xml.etree import ElementTree

import cv2 as cv
import numpy as np
from PIL import Image


def straight_alpha(image):
    rgba = np.array(image.convert('RGBA'))
    alpha = rgba[:, :, 3:4].astype(np.float32)
    rgba[:, :, :3] = np.clip(np.rint(rgba[:, :, :3] * 255.0 / np.maximum(alpha, 1)), 0, 255).astype(np.uint8)
    return Image.fromarray(rgba)


def pack_sprites(frames, size, rows, cols):
    w, h = size
    cw, ch = w * 2 // cols, h * 2 // rows
    if w % cols or h % rows or len(frames) != rows * cols:
        raise ValueError('sprite grid does not match the PC texture')
    out = Image.new('RGBA', (w * 2, h * 2))
    for i, frame in enumerate(frames):
        out.paste(frame.resize((cw, ch), Image.Resampling.LANCZOS), (i % cols * cw, i // cols * ch))
    return out


def register_background(pc, donor):
    w, h = pc.size
    sift = cv.SIFT_create(3500, contrastThreshold=0.012)
    source, target = [sift.detectAndCompute(np.array(im.convert('L')), None) for im in (donor, pc)]
    if source[1] is None or target[1] is None or len(target[1]) < 2:
        raise ValueError('not enough features')
    pairs = cv.BFMatcher().knnMatch(source[1], target[1], k=2)
    matches = [a for pair in pairs if len(pair) == 2 for a, b in [pair] if a.distance < .75 * b.distance]
    if len(matches) < 20:
        raise ValueError('not enough feature matches')
    p = np.float32([source[0][m.queryIdx].pt for m in matches])
    q = np.float32([target[0][m.trainIdx].pt for m in matches])
    matrix, inliers = cv.estimateAffine2D(p, q, method=cv.RANSAC, ransacReprojThreshold=2)
    if matrix is None:
        raise ValueError('no reliable alignment')
    points = q[inliers.ravel() != 0]
    dx, dy = matrix[0, 0], matrix[1, 1]
    if (len(points) < 20 or len(points) < .5 * len(matches) or min(np.ptp(points, axis=0) / [w, h]) < .30
            or not (.65 < dx <= 1 and .65 < dy <= 1) or abs(matrix[0, 1]) + abs(matrix[1, 0]) > .025):
        raise ValueError('no reliable alignment')
    rgb = np.array(donor.convert('RGB'))
    transform = matrix * 2
    transform[:, 2] += .5
    high = cv.warpAffine(rgb, transform, (w * 2, h * 2), flags=cv.INTER_CUBIC)
    stock = np.array(pc.convert('RGB').resize((w * 2, h * 2), Image.Resampling.LANCZOS))
    covered = cv.warpAffine(np.full(rgb.shape[:2], 255, np.uint8), transform, (w * 2, h * 2))
    lo = cv.GaussianBlur(high.max(axis=2), (0, 0), 4).astype(np.float32)
    ref = cv.GaussianBlur(stock.max(axis=2), (0, 0), 4).astype(np.float32)
    valid = (covered > 250) & ((lo >= 11) | (ref < 15)) & (lo + 13 >= .52 * ref)
    matte = cv.erode(valid.astype(np.float32), np.ones((13, 13), np.uint8), borderType=cv.BORDER_CONSTANT, borderValue=0)
    matte = cv.GaussianBlur(matte, (0, 0), 4)[:, :, None]
    if float(np.mean(matte)) < .55:
        raise ValueError('donor does not cover enough of the PC scene')
    merged = np.rint(high * matte + stock * (1 - matte)).astype(np.uint8)
    return Image.fromarray(merged), matrix, len(points)


def extend_background(image):
    rgb = np.array(image.convert('RGB'))
    h, w = rgb.shape[:2]
    width = 2880
    if w > width or (width - w) % 2:
        raise ValueError('invalid panorama geometry')
    pad = (width - w) // 2
    bright = np.mean(rgb.max(axis=2) > 16, axis=0)
    left, right = 0, w
    while left + 1 < min(w // 5, 180) and bright[left] < .25:
        left += 1
    while w - right + 1 < min(w // 5, 180) and bright[right - 1] < .25:
        right -= 1
    if bright[left] < .25:
        left = 0
    if bright[right - 1] < .25:
        right = w
    field_half = 650
    start = min(left, max(0, w // 2 - field_half))
    end = max(right, min(w, w // 2 + field_half))
    scene = rgb[:, start:end]
    canvas = cv.copyMakeBorder(scene, 0, 0, pad + start, pad + w - end, cv.BORDER_REFLECT_101)
    blurred = cv.GaussianBlur(canvas, (0, 0), 21)
    x = np.arange(width)
    dist = np.maximum(pad + start - x, x - (pad + end - 1)).clip(0, None)
    t = np.clip(dist / 235.0, 0, 1)
    t = (t * t * (3 - 2 * t))[None, :, None]
    return Image.fromarray(np.rint(canvas * (1 - t) + blurred * t * .78).astype(np.uint8))


def local_path(root, path):
    p = PurePosixPath(path.replace('\\', '/'))
    if p.is_absolute() or '..' in p.parts or ':' in p.as_posix() or not p.name:
        raise ValueError(f'invalid asset path: {path}')
    return root.joinpath(*p.parts)


class Donor:
    def __init__(self, root):
        self.root = root
        eng = root / 'en.lproj'
        manifest = ElementTree.parse(eng / 'properties/1024x768/resources.xml')
        self.files = {p.relative_to(root).as_posix().casefold(): p for p in root.rglob('*') if p.is_file()}
        self.sprites = {}
        for node in manifest.iter('image'):
            a = node.attrib
            if 'path' in a and 'id' in a:
                self.sprites[a['id'].upper()] = (a['path'], a, None)
        for atlas in manifest.iter('atlas'):
            for node in atlas.findall('atlas-entry'):
                a = node.attrib
                x, y, w, h = (int(a[k]) for k in ('x', 'y', 'width', 'height'))
                self.sprites[a['id'].upper()] = (atlas.attrib['path'], a, (x, y, x + w, y + h))
        self.backgrounds = {p.stem.upper(): p for p in (root / 'levels/1024x768').glob('*.jpg')}

    @lru_cache(maxsize=24)
    def read(self, path):
        stem = local_path(Path(), path).with_suffix('').as_posix().casefold()
        for folder in ('', 'en.lproj/'):
            for ext in ('.png', '.jpg', '.gif'):
                p = self.files.get(folder + stem + ext)
                if p:
                    with Image.open(p) as img:
                        return img.convert('RGBA')
        return None

    def frames(self, name, rows, cols):
        for ids in ([name + str(i) + '_LARGE' for i in range(rows * cols)],
                    [name + str(i) for i in range(rows * cols)], [name]):
            if not all(key in self.sprites for key in ids):
                continue
            frames = []
            for key in ids:
                path, attrs, box = self.sprites[key]
                sheet = self.read(path)
                if sheet is None:
                    break
                if box and (min(box) < 0 or box[2] > sheet.width or box[3] > sheet.height or box[0] >= box[2] or box[1] >= box[3]):
                    break
                im = straight_alpha(sheet.crop(box)) if box else sheet
                r, c = int(attrs.get('rows', '1')), int(attrs.get('cols', '1'))
                if r < 1 or c < 1 or (len(ids) == 1 and (r, c) != (rows, cols)) or im.width % c or im.height % r:
                    break
                w, h = im.width // c, im.height // r
                frames += [im.crop((x * w, y * h, (x + 1) * w, (y + 1) * h)) for y in range(r) for x in range(c)]
            else:
                if len(frames) == rows * cols:
                    return frames
        return None


def sprite_matches(pc, candidate, rows, cols, name=''):
    def on_gray(im):
        bg = Image.new('RGBA', im.size, (72, 72, 72, 255))
        bg.alpha_composite(im)
        return np.asarray(bg.convert('RGB'), dtype=np.float32)
    board_piece = name in ('IMAGE_BALL', 'IMAGE_BALLPEG', 'IMAGE_BALLPEGCB', 'IMAGE_BRICK', 'IMAGE_BRICKCB')
    w, h = pc.width // cols, pc.height // rows
    for r in range(rows):
        for c in range(cols):
            a = pc.crop((c * w, r * h, (c + 1) * w, (r + 1) * h))
            b = candidate.crop((2 * c * w, 2 * r * h, 2 * (c + 1) * w, 2 * (r + 1) * h)).resize((w, h), Image.Resampling.LANCZOS)
            ap, bp = np.asarray(a), np.asarray(b)
            mask = (ap[:, :, 3] > 24) | (bp[:, :, 3] > 24)
            if not mask.any():
                continue
            intersection = np.count_nonzero((ap[:, :, 3] > 24) & (bp[:, :, 3] > 24)) / int(mask.sum())
            x, y = on_gray(a)[mask], on_gray(b)[mask]
            if intersection < (.67 if board_piece else .72) or np.mean(np.abs(x - y)) > (55 if board_piece else 27):
                return False
    return True


def load_pc(path):
    with Image.open(path) as im:
        return im.convert('RGBA')


def build(reference, source, output):
    reference, source, output = (p.resolve() for p in (reference, source, output))
    if output.is_relative_to(reference) or output.is_relative_to(source):
        raise ValueError('choose a new output folder outside the input folders')
    edition, *lines = (reference / 'art.tsv').read_text(encoding='utf-8').splitlines()
    if edition not in ('Deluxe', 'Nights'):
        raise ValueError('not a Peggle PC reference folder')
    donor = Donor(source)
    provenance = {'edition': edition, 'sprites': {}, 'backgrounds': {}, 'stock_backgrounds': []}
    output.mkdir(parents=True)

    def save(path, im):
        dst = local_path(output, path)
        dst.parent.mkdir(parents=True, exist_ok=True)
        im.save(dst)

    backgrounds = []
    for line in lines:
        name, path, rows, cols = line.split('\t')
        rows, cols = int(rows), int(cols)
        if name == 'BACKGROUND':
            backgrounds.append(path)
            continue
        if rows < 1 or cols < 1 or rows * cols > 512 or path in provenance['sprites']:
            continue
        frames = donor.frames(name, rows, cols)
        if not frames:
            continue
        pc = load_pc(local_path(reference, path))
        w, h = pc.size
        if w % cols or h % rows or any(f.width < 1.12 * w / cols or f.height < 1.12 * h / rows for f in frames):
            continue
        candidate = pack_sprites(frames, pc.size, rows, cols)
        if not sprite_matches(pc, candidate, rows, cols, name):
            continue
        save(path, candidate)
        provenance['sprites'][path] = name

    def convert(path):
        p = donor.backgrounds.get(PurePosixPath(path).stem.upper())
        if not p:
            return path, None, 'no HD source'
        pc = load_pc(local_path(reference, path))
        with Image.open(p) as artwork:
            try:
                im, matrix, inliers = register_background(pc, artwork)
            except ValueError as error:
                return path, None, str(error)
        save(path, im)
        save('widescreen/' + path, extend_background(im))
        return path, {'donor': p.name, 'alignment': matrix.tolist(), 'matched_landmarks': inliers}, None

    cv.setNumThreads(1)
    with ThreadPoolExecutor(max_workers=6) as pool:
        for path, data, reason in pool.map(convert, backgrounds):
            if data:
                provenance['backgrounds'][path] = data
            else:
                provenance['stock_backgrounds'].append({'path': path, 'reason': reason})
    if not provenance['backgrounds'] or not provenance['sprites']:
        raise ValueError('no compatible HD art; check the iPad source and matching PC reference')
    (output / 'hd-art-report.json').write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')
    (output / 'mod.ini').write_text(f'id = peggle_native_hd_{edition.lower()}\nname = Peggle {edition} Native HD\n'
                                   'version = 0.2\napi = 1\nimage_scale = 2\nimage_alpha = baked\n'
                                   'determinism = cosmetic\ndescription = Higher-detail iPad artwork adapted to the PC layout.\n',
                                   encoding='utf-8')
    print(f'{edition}: {len(provenance["sprites"])} HD textures, {len(provenance["backgrounds"])} HD backgrounds, '
          f'{len(provenance["stock_backgrounds"])} backgrounds left stock.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Import existing iPad Peggle HD artwork into a PegglePort data mod.')
    parser.add_argument('pc_reference', type=Path)
    parser.add_argument('ipad_content_hd', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    try:
        build(args.pc_reference, args.ipad_content_hd, args.output)
    except (ValueError, OSError, ElementTree.ParseError) as exc:
        parser.exit(1, f'HD art was not imported: {exc}\n')
