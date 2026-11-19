import subprocess
import sys
import tempfile
from pathlib import Path


def check(executable, collision):
    with tempfile.TemporaryDirectory() as temp:
        parent = Path(temp)
        source = parent / 'stock'
        source.mkdir()
        pak = bytes(b ^ 0xf7 for b in bytes.fromhex('c0 4a c0 ba 00 00 00 00 80'))
        (source / 'main.pak').write_bytes(pak)
        fixture = parent / 'navigation'
        sentinel = (fixture if collision else parent) / 'keep.txt'
        if collision:
            fixture.mkdir()
        sentinel.write_bytes(b'untouched')
        result = subprocess.run([executable, source, source, parent / 'missing-art', parent],
                                capture_output=True, text=True, timeout=30)
        assert sentinel.read_bytes() == b'untouched' and (source / 'main.pak').read_bytes() == pak
        if collision:
            assert list(fixture.iterdir()) == [sentinel] and set(parent.iterdir()) == {source, fixture}
            expected = 'Need a fresh navigation fixture'
        else:
            assert set(parent.iterdir()) == {source, sentinel}, 'partial fixture survived failed presentation mount'
            expected = 'asset mount'
        assert result.returncode == 1 and expected in result.stderr, (result.returncode, result.stderr)
        print('PASS', 'refused occupied child' if collision else 'cleaned after failed mount')


if __name__ == '__main__':
    exe = Path(sys.argv[1]).resolve(strict=True)
    for collision in (False, True):
        check(exe, collision)
