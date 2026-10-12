"""Exercise CLI checkpoints, malformed files, sweeps, and recurrence end to end."""
from pathlib import Path
import csv
import io
import struct
import subprocess
import sys
import tempfile

exe = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='leniascope-cli-') as tmp:
    root = Path(tmp)
    # A Windows executable invoked from WSL needs Windows paths for its data files.
    def arg_path(path):
        if sys.platform == 'linux' and exe.suffix.lower() == '.exe':
            return subprocess.check_output(['wslpath', '-w', str(path)], text=True).strip()
        return str(path)

    def run(*args, succeeds=True):
        result = subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True)
        if (result.returncode == 0) != succeeds:
            raise RuntimeError(result.stdout + result.stderr)
        return result.stdout

    state = root / 'initial.lenia'
    run('run', '--dimensions', 3, '--size', 16, '--radius', 3, '--steps', 0,
        '--output', arg_path(state))
    data = state.read_bytes()
    assert data[:8] == b'LeniaS4\0'
    recipe_offset = 96 + struct.unpack_from('<I', data, 84)[0]
    assert struct.unpack_from('<Iii4d', data, recipe_offset) == (1, 10, 3, .6, 1.2, 0., 1.)
    # Remove just the new metadata to make a genuine version-2 checkpoint.
    legacy = root / 'version2.lenia'
    legacy.write_bytes(b'LeniaS2\0' + data[8:recipe_offset] + data[recipe_offset+72:])
    comparison = run('compare', arg_path(legacy), arg_path(state))
    assert float(comparison.splitlines()[1].split(',')[-1]) < 1e-14
    version3 = root / 'version3.lenia'
    version3.write_bytes(b'LeniaS3\0' + data[8:recipe_offset+44] + data[recipe_offset+72:])
    migrated = root / 'migrated.lenia'
    run('run', '--input', arg_path(version3), '--steps', 0, '--output', arg_path(migrated))
    assert migrated.read_bytes() == data  # Old recipes acquire the uniform placement defaults.
    upgraded = root / 'upgraded.lenia'
    run('run', '--input', arg_path(legacy), '--steps', 0, '--output', arg_path(upgraded))
    upgraded_data = upgraded.read_bytes()
    assert struct.unpack_from('<I', upgraded_data, 96 + struct.unpack_from('<I', upgraded_data, 84)[0])[0] == 0
    cases = [b'wrong!!!' + data[8:], data[:-1], data + b'garbage']
    for offset, fmt, value in [(recipe_offset, '<I', 2), (recipe_offset+4, '<i', 4097), (recipe_offset+12, '<d', float('nan')), (recipe_offset+8, '<i', 0)]:
        damaged = bytearray(data)
        struct.pack_into(fmt, damaged, offset, value)
        cases.append(damaged)
    for offset, fmt, value in [(8, '<i', -1), (32, '<d', float('nan')), (84, '<I', 999999)]:
        damaged = bytearray(data)
        struct.pack_into(fmt, damaged, offset, value)
        cases.append(damaged)
    for index, content in enumerate(cases):
        invalid = root / f'invalid-{index}.lenia'
        invalid.write_bytes(content)
        run('run', '--input', arg_path(invalid), '--steps', 0, succeeds=False)

    # Resume must agree with an uninterrupted trajectory at the same seed and parameters.
    one, two, full = (root / name for name in ['one.lenia', 'two.lenia', 'full.lenia'])
    run('run', '--input', arg_path(state), '--steps', 3, '--output', arg_path(one))
    run('run', '--input', arg_path(one), '--steps', 4, '--output', arg_path(two))
    run('run', '--input', arg_path(state), '--steps', 7, '--output', arg_path(full))
    comparison = run('compare', arg_path(two), arg_path(full))
    assert float(comparison.splitlines()[1].split(',')[-1]) < 1e-14

    csv_path = root / 'sweep.csv'
    run('run', '--input', arg_path(state), '--steps', 4, '--sample-every', 2,
        '--sweep-mu', '0.14,0.15', '--csv', arg_path(csv_path))
    for index, mu in enumerate([0.14, 0.15]):
        with Path(str(csv_path) + f'.run-{index}').open() as stream:
            rows = list(csv.DictReader(stream))
        assert len(rows) == 3
        assert all(float(row['mu']) == mu for row in rows)
        assert [int(row['step']) for row in rows] == [0, 2, 4]
    run('run', '--unknown', 10, succeeds=False)
    run('run', '--steps', -1, succeeds=False)
    report = list(csv.DictReader(io.StringIO(run('converge', arg_path(state), '--steps', 4, '--levels', 3))))
    assert [int(row['steps']) for row in report] == [4, 8, 16]
    assert [float(row['dt']) for row in report] == [.1, .05, .025]
    assert all(float(row['duration']) == .4 for row in report)
    assert float(report[-1]['rms_to_finest']) == 0
    assert state.read_bytes() == data  # The study never changes the source experiment.
    for args in [('--levels', 1), ('--steps', 0), ('--steps', '3x'), ('--unknown', 3)]:
        run('converge', arg_path(state), *args, succeeds=False)
    custom = root / 'custom.lenia'
    custom_csv = root / 'custom.csv'
    recipe_args = ('--patches', 23, '--patch-min', .8, '--patch-max', 1.5, '--value-min', .2, '--value-max', .7)
    run('run', '--size', 16, '--radius', 3, '--seed', 77, '--steps', 0, *recipe_args,
        '--output', arg_path(custom), '--csv', arg_path(custom_csv))
    custom_data = custom.read_bytes()
    offset = 96 + struct.unpack_from('<I', custom_data, 84)[0]
    assert struct.unpack_from('<Iii4d', custom_data, offset) == (1, 23, 3, .8, 1.5, .2, .7)
    row = next(csv.DictReader(custom_csv.open()))
    assert row['schema'] == '4' and int(row['seed_patches']) == 23 and float(row['seed_value_min']) == .2
    for args in [('--patches', -1), ('--patch-min', 2), ('--value-max', 2), ('--patches', 5000)]:
        run('run', '--size', 16, '--radius', 3, '--steps', 0, *args, succeeds=False)
    run('run', '--input', arg_path(custom), '--steps', 0, '--patches', 10, succeeds=False)
    assert int(row['seed_radius']) == 3
    resumed_custom = root / 'resumed-custom.lenia'
    run('run', '--input', arg_path(custom), '--steps', 0, '--output', arg_path(resumed_custom))
    assert resumed_custom.read_bytes() == custom_data
    changed_radius = root / 'changed-radius.lenia'
    run('run', '--input', arg_path(custom), '--radius', 4, '--steps', 0, '--output', arg_path(changed_radius))
    changed = changed_radius.read_bytes()
    assert struct.unpack_from('<i', changed, 20)[0] == 4
    assert struct.unpack_from('<i', changed, offset+8)[0] == 3 # Preserve the actual seeding radius.
    for dimensions in (2, 3):
        for layout in ('cluster', 'pair'):
            field, trace, replay = (root / f'{layout}-{dimensions}{suffix}' for suffix in ('.lenia', '.csv', '-replay.lenia'))
            recipe = ('--layout', layout, '--spread', .8, '--angle', -30, '--elevation', 20)
            run('run', '--dimensions', dimensions, '--size', 32, '--radius', 5, '--seed', 42,
                '--steps', 0, *recipe, '--output', arg_path(field), '--csv', arg_path(trace))
            record = next(csv.DictReader(trace.open()))
            assert record['seed_layout'] == ('1' if layout == 'cluster' else '2')
            assert record['seed_patches'] == ('10' if layout == 'cluster' else '2')
            assert [float(record[k]) for k in ('seed_spread','seed_angle','seed_elevation')] == [.8, -30, 20]
            run('run', '--input', arg_path(field), '--steps', 0, '--output', arg_path(replay))
            assert replay.read_bytes() == field.read_bytes()
    for bad in [('--layout','unknown'), ('--layout','pair','--patches',3), ('--spread',-1),
                ('--spread','nan'), ('--angle',181), ('--elevation',91)]:
        run('run','--size',16,'--radius',3,'--steps',0,*bad,succeeds=False)
    for flag, value in [('--layout','pair'), ('--spread',1), ('--angle',30), ('--elevation',20)]:
        run('run','--input',arg_path(custom),'--steps',0,flag,value,succeeds=False)
    if '--cuda' in sys.argv[2:]:
        for backend in ('gpu', 'cuda'):
            full, partial, resumed = (root / f'{backend}-{part}.lenia' for part in ('full', 'partial', 'resumed'))
            trace = root / f'{backend}.csv'
            run('run', '--input', arg_path(custom), '--backend', backend, '--steps', 12, '--output', arg_path(full), '--csv', arg_path(trace))
            run('run', '--input', arg_path(custom), '--backend', backend, '--steps', 5, '--output', arg_path(partial))
            run('run', '--input', arg_path(partial), '--backend', backend, '--steps', 7, '--output', arg_path(resumed))
            assert full.read_bytes() == resumed.read_bytes(), f'{backend} checkpoint continuation differs'
            records = list(csv.DictReader(trace.open()))
            assert all(r['backend'] == backend and r['seed_patches'] == '23' for r in records)
        print('PASS CUDA/DirectCompute CLI checkpoints, exact continuation and CSV backend labels')
    for backend in ['cpu'] + (['gpu', 'cuda'] if '--cuda' in sys.argv[2:] else []):
        report_dir = root / f'study-{backend}'
        stdout = run('study', arg_path(custom), '--steps', 7, '--sample-every', 3,
                     '--backend', backend, '--output', arg_path(report_dir))
        rows = list(csv.DictReader(io.StringIO(stdout)))
        assert [int(r['step']) for r in rows] == [0,3,6,7]
        assert abs(float(rows[-1]['elapsed']) - .7) < 1e-14
        assert stdout == (report_dir / 'samples.csv').read_text()
        metadata = (report_dir / 'report.txt').read_text()
        assert metadata.startswith('LeniascopeBehavior 1\n') and custom.name in metadata
        assert custom.read_bytes() == custom_data
        run('study', arg_path(custom), '--output', arg_path(report_dir), succeeds=False)
        assert (report_dir / 'report.txt').read_text() == metadata
    for args in [('--steps',0), ('--steps','7x'), ('--sample-every',0), ('--threshold','nan'),
                 ('--backend','unknown'), ('--steps',1000001), ('--sample-every',1), ('--unknown',1)]:
        destination = root / 'invalid-study'
        run('study', arg_path(custom), '--output', arg_path(destination), *args, succeeds=False)
        assert not destination.exists()
    print('PASS behavior CLI sampling, backend provenance, source preservation and output validation')
    print('PASS seed layouts/recipes, v2/v3 migration, CSV metadata; CLI malformed checkpoints, resume equivalence, recurrence, sweep CSV, timestep study, invalid options')
