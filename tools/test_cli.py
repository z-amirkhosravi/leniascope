"""Exercise CLI checkpoints, malformed files, sweeps, and recurrence end to end."""
from pathlib import Path
import csv
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
    cases = [b'wrong!!!' + data[8:], data[:-1], data + b'garbage']
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
    print('PASS CLI malformed checkpoints, resume equivalence, recurrence, sweep CSV, invalid options')
