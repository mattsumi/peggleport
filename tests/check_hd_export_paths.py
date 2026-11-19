import subprocess
import sys
import tempfile
from pathlib import Path


def check(executable):
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        game, checkout = root / 'game', root / 'checkout'
        (game / 'levels').mkdir(parents=True)
        (game / 'properties').mkdir()
        (game / 'main.pak').write_bytes(bytes(b ^ 0xf7 for b in bytes.fromhex('c04ac0ba0000000080')))
        (game / 'properties/resources.xml').write_text('<Image id="IMAGE_TEST" path="levels/test"/>')
        (game / 'levels/stages.cfg').write_text('Stage\nLevel: "_test"\n')
        (game / 'levels/_test.dat').write_bytes(bytes(9))
        (game / 'levels/_test.ini').write_text('background = test\n')
        (game / 'levels/test.tga').write_bytes(bytes.fromhex('000002000000000000000000010001001820010203'))
        checkout.mkdir()
        (checkout / '.git').touch()

        def export(output, cwd=root):
            return subprocess.run([executable, 'Deluxe', game, output], cwd=cwd, capture_output=True, text=True)

        staged = root / 'reference'
        assert export(staged).returncode == 0
        index = (staged / 'art.tsv').read_bytes()
        assert b'IMAGE_TEST' in index and (staged / 'LEVELS/TEST.tga').is_file()
        assert export(staged).returncode != 0 and (staged / 'art.tsv').read_bytes() == index
        assert export(game / 'reference').returncode != 0 and not (game / 'reference').exists()
        parent = root / 'ordinary'
        parent.mkdir()
        result = export(parent / '..' / 'ambiguous-output')
        assert result.returncode != 0 and 'without ..' in result.stderr, result
        assert not (root / 'ambiguous-output').exists()
        for output, cwd in ((checkout / 'nested/reference', root), ('relative', checkout), (checkout, root)):
            result = export(output, cwd)
            assert result.returncode != 0 and 'outside any Git checkout' in result.stderr, (output, result)
        assert list(checkout.iterdir()) == [checkout / '.git']


if __name__ == '__main__':
    check(Path(sys.argv[1]).resolve(strict=True))
