import subprocess
import sys
import tempfile
from pathlib import Path


def check(executable, collision):
    with tempfile.TemporaryDirectory() as temp:
        parent = Path(temp)
        keep = parent / 'keep.txt'
        keep.write_bytes(b'keep')
        child = parent / 'pad-glyphs'
        if collision:
            child.mkdir()
            (child / 'existing.txt').write_bytes(b'existing')
        result = subprocess.run([executable, parent])
        assert keep.is_file() and keep.read_bytes() == b'keep', 'caller sentinel lost'
        if collision:
            assert result.returncode == 1 and (child / 'existing.txt').read_bytes() == b'existing'
        else:
            assert result.returncode == 0 and list(parent.iterdir()) == [keep]


if __name__ == '__main__':
    for collision in (False, True):
        check(Path(sys.argv[1]).resolve(strict=True), collision)
