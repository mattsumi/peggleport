import argparse
from pathlib import Path
import re
import shutil
import tempfile


def build_pack(source, output):
    src = source.resolve(strict=True) / "levels"
    out = output.resolve()
    if out.exists() or out.is_relative_to(source.resolve()):
        raise ValueError("Choose a new mod folder outside the source installation.")
    cfg = (src / "stages.cfg").read_text(encoding="latin-1")
    cfg = re.sub(r'/\*.*?\*/|//[^\n]*', '', cfg, flags=re.S)
    stages = []
    names = set()
    for stage in re.findall(r'^\s*Stage\s*\{([^}]*)\}', cfg, re.M | re.I):
        levels = []
        for line in re.findall(r'^\s*Level:[ \t]*([^\r\n]+)', stage, re.M | re.I):
            old, _, title = line.partition(',')
            old = old.strip().lower()
            if not re.fullmatch(r'[a-z0-9_]+', old) or old in names:
                raise ValueError(f"Invalid or duplicate prototype level: {old}")
            names.add(old)
            levels.append((old, title.strip() or f'"{old}"'))
        if levels:
            stages.append(levels)
    if len(names) != 55:
        raise ValueError(f"Expected 55 listed levels in the August 2006 prototype; found {len(names)}.")

    def images(folder):
        return {p.name.lower(): p for p in folder.iterdir() if p.is_file()} if folder.is_dir() else {}

    data = images(src)
    thumbnails = images(src / "cached_thumbnails")
    out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=out.parent) as tmp:
        pack = Path(tmp) / out.name
        dst = pack / "levels"
        previews = dst / "cached_thumbnails"
        previews.mkdir(parents=True)
        shutil.copyfile(data["_base.dat"], dst / "_pego_base.dat")
        stage_index = []
        for i, levels in enumerate(stages, 1):
            stage_index += ['Stage\n{', f'  Name: "Pego - Stage {i}"']
            for old, title in levels:
                name = "pego_" + old
                stage_index.append(f"  Level: {name}, {title}")
                shutil.copyfile(data[old + ".dat"], dst / (name + ".dat"))
                for pool, target in ((data, dst), (thumbnails, previews)):
                    for ext in (".jp2", ".jpg", ".png", ".gif"):
                        if old + ext in pool:
                            shutil.copyfile(pool[old + ext], target / (name + ext))
                background = name if any((dst / (name + e)).is_file() for e in (".jp2", ".jpg", ".png", ".gif")) else old
                (dst / (name + ".ini")).write_text(f"base = _pego_base\nbackground = {background}\n", encoding="ascii")
            stage_index.append('}')
        (dst / "mod_levels.cfg").write_text('\n'.join(stage_index) + '\n', encoding="latin-1")
        (pack / "mod.ini").write_text("id = pego_prototype\nname = Pego Prototype\nversion = 1.0\ndescription = 55 boards from the August 2006 Pego prototype\n", encoding="ascii")
        pack.rename(out)
    return len(stages), len(names)


def main():
    parser = argparse.ArgumentParser(description="Generate a local Pego Quick Play pack from your copy of the prototype.")
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        stages, count = build_pack(args.source, args.output)
    except (OSError, ValueError, KeyError) as e:
        parser.exit(1, f"Could not build Pego pack: {e}\n")
    print(f"Created {count} Pego levels in {stages} stages at {args.output}")


if __name__ == "__main__":
    main()
