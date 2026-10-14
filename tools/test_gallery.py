"""Exercise gallery capture/preview/save/browse/load through the actual application's smoke path.

All fields and galleries are isolated. Existing windows and personal workspaces are untouched.
"""
from pathlib import Path
import argparse
import csv
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('gui', type=Path)
parser.add_argument('cli', type=Path)
parser.add_argument('--cuda', action='store_true')
parser.add_argument('--output-dir', type=Path)
args = parser.parse_args()
gui, cli = args.gui.resolve(), args.cli.resolve()

def native(p):
    import sys
    if sys.platform == 'linux':
        return subprocess.check_output(['wslpath', '-w', str(p)], text=True).strip()
    return str(p)

def checksum(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & ((1 << 64)-1)
    return value

def exercise(root):
    for backend in ['cpu', 'gpu'] + (['cuda'] if args.cuda else []):
        for active in (0, 1):
            case = root / f'{backend}-{active+2}d'
            profile = case / 'workspace'
            slot = profile / 'recovery-a'
            slot.mkdir(parents=True)
            original = []
            for dimension in (2, 3):
                field = slot / f'field-{dimension}d.lenia'
                subprocess.run([str(cli), 'run', '--dimensions', str(dimension), '--size', '16',
                    '--radius', '3', '--seed', str(dimension+100), '--steps', '7', '--backend', backend,
                    '--layout', 'pair' if dimension == 2 else 'cluster', '--spread', '.8',
                    '--patches', '2' if dimension == 2 else '23', '--patch-min', '.8', '--patch-max', '1.5', '--value-min', '.2',
                    '--value-max', '.7', '--output', native(field)], check=True, capture_output=True)
                original.append(field.read_bytes())
            (slot / 'complete.txt').write_text(f'LeniascopeRecovery 1\n1 1 1 {active} {checksum(original[0])} {checksum(original[1])}\n')
            settings = (f'LeniascopeWorkspace 4\n75 {int(backend!="cpu")} {active} 1 1100 800 0\n'
                '0 2 3 0.3 0.65 0.3 1.9 2 0.02 1 0.75 2 0.1 0.2 0.3\n'
                '2 1 20 0.6 -1.2 0.4 2.5 3 0.15 0.8 0.5 1 0.2 0.3 0.4\n'
                f'2 .8 1.5 .2 .7 0 1\n23 .8 1.5 .2 .7 1 0\n{int(backend=="cuda")}\n2 .8 0 0\n1 .8 0 0\n')
            (profile / 'preferences.txt').write_text(settings)
            result = subprocess.run([str(gui), '--gallery-smoke', '--workspace', native(profile)],
                cwd=case, capture_output=True, timeout=90)
            if result.returncode:
                error = case / 'leniascope-error.log'
                raise RuntimeError(error.read_text() if error.exists() else str(result))
            entries = [p for p in (profile / 'gallery').iterdir() if not p.name.startswith('.')]
            assert len(entries) == 1
            entry = entries[0]
            assert (entry / 'state.lenia').read_bytes() == original[active]
            text = (entry / 'entry.txt').read_text()
            assert text.startswith('LeniascopeGallery 2\n') and 'Smoke reference' in text and 'test, periodic' in text
            bmp = (entry / 'preview.bmp').read_bytes()
            assert bmp[:2] == b'BM' and len(bmp) == 54 + 384*288*4
            studies = [p for p in (entry / 'studies').iterdir() if not p.name.startswith('.')]
            assert len(studies) == 1
            report = (studies[0] / 'report.txt').read_text()
            assert report.startswith('LeniascopeBehavior 2\n') and entry.name in report
            rows = list(csv.DictReader((studies[0] / 'samples.csv').open()))
            assert [int(row['step']) for row in rows] == [0, 3, 6, 7]
            assert float(rows[0]['raw_rms']) == 0 and float(rows[0]['aligned_rms']) == 0
            assert all(r['components'] != '' and r['active_cells'] != '' for r in rows)
            batches = list((entry / 'batches').iterdir())
            assert len(batches) == 1
            batch = batches[0]
            assert (batch / 'complete.txt').exists()
            assert (batch / 'source.lenia').read_bytes() == original[active]
            batch_rows = list(csv.DictReader((batch / 'summary.csv').open()))
            assert len(batch_rows) == 2 and [float(r['value']) for r in batch_rows] == [.8, 1.5]
            assert batch_rows[0]['label'] == 'smoke assessment'
            for i, row in enumerate(batch_rows):
                assert row['status'] == 'complete' and row['steps'] == '5' and row['seed'] == '216'
                assert row['components'] != '' and row['winding_components'] != ''
                run = batch / f'run-{i+1:04d}'
                for kind in ['initial', 'final']:
                    assert checksum((run / f'{kind}.lenia').read_bytes()) == int(row[f'{kind}_hash'])
            manifest = (profile / 'recovery-b/complete.txt').read_text().split()
            assert manifest[:6] == ['LeniascopeRecovery', '2', '2', '1', '1', str(active)]
            assert manifest[8:10] == [str(int(active==0)), str(int(active==1))]
            initial = profile / f'recovery-b/initial-{active+2}d.lenia'
            assert initial.read_bytes() == original[active], 'Gallery field was not retained as Reset initial state'
            assert str(checksum(initial.read_bytes())) == manifest[10+active]
            for i, dimension in enumerate((2, 3)):
                assert (profile / f'recovery-b/field-{dimension}d.lenia').read_bytes() == original[i]
            actual = (profile / 'preferences.txt').read_text().splitlines()
            for a, b in zip(actual[2:], settings.splitlines()[2:]):
                assert len(a.split()) == len(b.split())
                assert all(abs(float(x)-float(y)) < 1e-6 for x,y in zip(a.split(), b.split()))
            assert (case / 'ui-smoke.bmp').stat().st_size > 10000
            for image in ['shape-components.bmp', 'shape-largest.bmp']:
                bitmap = (profile / image).read_bytes()
                assert bitmap[:2] == b'BM' and len(bitmap) == 54 + 320*240*4
            print(f'PASS {backend} {active+2}D gallery GUI: capture/reload, behavior study, batch/label/reopen/endpoint load, unchanged live/inactive fields', flush=True)

if args.output_dir:
    args.output_dir.resolve().mkdir(parents=True, exist_ok=False)
    exercise(args.output_dir.resolve())
else:
    with tempfile.TemporaryDirectory(prefix='leniascope-gallery-gui-') as folder:
        exercise(Path(folder))
