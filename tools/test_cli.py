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
    assert data[:8] == b'LeniaS3\0'
    recipe_offset = 96 + struct.unpack_from('<I', data, 84)[0]
    assert struct.unpack_from('<Iii4d', data, recipe_offset) == (1, 10, 3, .6, 1.2, 0., 1.)
    # Remove just the new metadata to make a genuine version-2 checkpoint.
    legacy = root / 'version2.lenia'
    legacy.write_bytes(b'LeniaS2\0' + data[8:recipe_offset] + data[recipe_offset+44:])
    comparison = run('compare', arg_path(legacy), arg_path(state))
    assert float(comparison.splitlines()[1].split(',')[-1]) < 1e-14
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
    assert row['schema'] == '3' and int(row['seed_patches']) == 23 and float(row['seed_value_min']) == .2
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
    print('PASS seed recipes, v2 migration, CSV metadata; CLI malformed checkpoints, resume equivalence, recurrence, sweep CSV, timestep study, invalid options')
