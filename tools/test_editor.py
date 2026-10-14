"""Exercise draft discard, copy/rotate/stamp, undo/redo, apply and restart in the real GUI.

Uses the application's own --editor-smoke path and isolated workspaces. No desktop
input automation or personal gallery writes. Requires a Windows graphics adapter.
"""
from pathlib import Path
import argparse
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('gui', type=Path)
parser.add_argument('cli', type=Path)
parser.add_argument('--cuda', action='store_true')
parser.add_argument('--output-dir', type=Path)
args = parser.parse_args()
gui, cli = args.gui.resolve(), args.cli.resolve()


def native(path):
    if sys.platform == 'linux':
        return subprocess.check_output(['wslpath', '-w', str(path)], text=True).strip()
    return str(path)


def checksum(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return value


def exercise(root):
    for backend in ['cpu', 'gpu'] + (['cuda'] if args.cuda else []):
        case = root / backend
        profile = case / 'workspace'
        slot = profile / 'recovery-a'
        slot.mkdir(parents=True)
        original = []
        for dimension in (2, 3):
            field = slot / f'field-{dimension}d.lenia'
            subprocess.run([str(cli), 'run', '--dimensions', str(dimension), '--size', '16',
                '--radius', '3', '--seed', str(dimension+100), '--steps', '7', '--backend', backend,
                '--patches', '23', '--patch-min', '.8', '--patch-max', '1.5', '--value-min', '.2',
                '--value-max', '.7', '--output', native(field)], check=True, capture_output=True)
            original.append(field.read_bytes())
        (slot / 'complete.txt').write_text(
            f'LeniascopeRecovery 1\n1 1 1 0 {checksum(original[0])} {checksum(original[1])}\n')
        settings = (f'LeniascopeWorkspace 4\n75 {int(backend!="cpu")} 0 1 1100 800 0\n'
            '0 2 3 0.3 0.65 0.3 1.9 2 0.02 1 0.75 2 0.1 0.2 0.3\n'
            '2 1 20 0.6 -1.2 0.4 2.5 3 0.15 0.8 0.5 1 0.2 0.3 0.4\n'
            f'23 .8 1.5 .2 .7 0 1\n23 .8 1.5 .2 .7 1 0\n{int(backend=="cuda")}\n0 1.5 0 0\n0 1.5 0 0\n')
        (profile / 'preferences.txt').write_text(settings)
        for launch, flag in enumerate(('--editor-smoke', '--smoke')):
            result = subprocess.run([str(gui), flag, '--workspace', native(profile)],
                cwd=case, capture_output=True, timeout=90)
            if result.returncode:
                error = case / 'leniascope-error.log'
                raise RuntimeError(error.read_text() if error.exists() else repr(result))
            expected = (case / 'editor-expected.lenia').read_bytes()
            assert expected[:8] == b'LeniaS5\0' and expected != original[0]
            assert b'Field edit v1; parent=' in expected and b'bilinear-zero-extended' in expected
            slot = profile / ('recovery-b' if launch == 0 else 'recovery-a')
            manifest = (slot / 'complete.txt').read_text().split()
            assert manifest[:6] == ['LeniascopeRecovery', '2', str(launch+2), '1', '1', '0']
            assert manifest[8:10] == ['1', '0']
            assert (slot / 'initial-2d.lenia').read_bytes() == expected, 'Edited initial field lost on restart'
            assert manifest[10] == str(checksum(expected))
            for i, dimension in enumerate((2, 3)):
                saved = (slot / f'field-{dimension}d.lenia').read_bytes()
                assert saved == (expected if dimension == 2 else original[1]), 'Apply/restart changed field or history'
                assert str(checksum(saved)) == manifest[6+i]
            actual = (profile / 'preferences.txt').read_text().splitlines()
            assert actual[1].split()[:4] == settings.splitlines()[1].split()[:4], 'Backend/mode changed'
            for a, b in zip(actual[2:], settings.splitlines()[2:]):
                assert len(a.split()) == len(b.split())
                assert all(abs(float(x)-float(y)) < 1e-6 for x,y in zip(a.split(), b.split()))
            for file in ('ui-smoke.bmp', 'editor-ui-smoke.bmp', 'editor-preview.bmp'):
                assert (case / file).stat().st_size > 10000
        # Follow through capture/save/reload and a short study of the edited state.
        result = subprocess.run([str(gui), '--gallery-smoke', '--workspace', native(profile)],
            cwd=case, capture_output=True, timeout=90)
        if result.returncode:
            error = case / 'leniascope-error.log'
            raise RuntimeError(error.read_text() if error.exists() else repr(result))
        entries = [p for p in (profile / 'gallery').iterdir() if not p.name.startswith('.')]
        assert len(entries) == 1
        assert (entries[0] / 'state.lenia').read_bytes() == expected
        assert (entries[0] / 'entry.txt').read_text().startswith('LeniascopeGallery 3\n')
        print(f'PASS {backend} editor GUI: discard, periodic placement, rotations, exact undo/redo, apply, '
              'restart, edited gallery and study, inactive field and view preserved', flush=True)


if args.output_dir:
    args.output_dir.resolve().mkdir(parents=True, exist_ok=False)
    exercise(args.output_dir.resolve())
else:
    with tempfile.TemporaryDirectory(prefix='leniascope-editor-gui-') as folder:
        exercise(Path(folder))
