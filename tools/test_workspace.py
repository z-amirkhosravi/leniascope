"""Exercise real GUI startup, restore, and graceful save/close in an isolated workspace.

The application's own --smoke mode exports its rendered buffers and closes itself.
No desktop automation or existing personal workspace is used.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

gui, cli = (Path(arg).resolve() for arg in sys.argv[1:3])
gpu = "--gpu" in sys.argv[3:]

def native(path):
    if sys.platform == "linux" and gui.suffix.lower() == ".exe":
        return subprocess.check_output(["wslpath", "-w", str(path)], text=True).strip()
    return str(path)

def checksum(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return value

with tempfile.TemporaryDirectory(prefix="leniascope-gui-workspace-") as temporary:
    root = Path(temporary)
    profile = root / "workspace"
    slot = profile / "recovery-a"
    slot.mkdir(parents=True)
    originals = []
    for dimension in (2, 3):
        file = slot / f"field-{dimension}d.lenia"
        subprocess.run([str(cli), "run", "--dimensions", str(dimension), "--size", "16",
                        "--radius", "3", "--seed", str(100 + dimension), "--steps", "7",
                        "--backend", "gpu" if gpu else "cpu",
                        "--patches", "23", "--patch-min", ".8", "--patch-max", "1.5",
                        "--value-min", ".2", "--value-max", ".7",
                        "--output", native(file)], check=True, capture_output=True)
        originals.append(file.read_bytes())
    (slot / "complete.txt").write_text(
        f"LeniascopeRecovery 1\n1 1 1 0 {checksum(originals[0])} {checksum(originals[1])}\n")
    settings = (f"LeniascopeWorkspace 2\n75 {int(gpu)} 0 1 980 680 0\n"
                "0 2 3 0.3 0.65 0.3 1.9 2 0.02 1 0.75 2 0.1 0.2 0.3\n"
                "2 1 20 0.6 -1.2 0.4 2.5 3 0.15 0.8 0.5 1 0.2 0.3 0.4\n"
                "23 .8 1.5 .2 .7 0 1\n23 .8 1.5 .2 .7 1 0\n")
    (profile / "preferences.txt").write_text(settings)
    for launch in range(2):
        result = subprocess.run([str(gui), "--smoke", "--workspace", native(profile)],
                                cwd=root, capture_output=True, timeout=90)
        if result.returncode:
            error = root / "leniascope-error.log"
            raise RuntimeError(error.read_text() if error.exists() else repr(result))
        assert (root / "ui-smoke.bmp").stat().st_size > 10000
        assert (root / "volume-smoke.bmp").stat().st_size > 10000
        newest = profile / ("recovery-b" if launch == 0 else "recovery-a")
        manifest = (newest / "complete.txt").read_text().split()
        assert manifest[:6] == ["LeniascopeRecovery", "1", str(launch + 2), "1", "1", "0"]
        for i, dimension in enumerate((2, 3)):
            saved = (newest / f"field-{dimension}d.lenia").read_bytes()
            assert saved == originals[i], "Reopening advanced, changed, or discarded a paused field"
            assert str(checksum(saved)) == manifest[6+i]
        rows = (profile / "preferences.txt").read_text().splitlines()
        controls = rows[1].split()
        assert controls[:4] == ["75", str(int(gpu)), "0", "1"]
        for actual, expected in zip(rows[2:], settings.splitlines()[2:]):
            assert all(abs(float(a)-float(b)) < 1e-6 for a, b in zip(actual.split(), expected.split()))
    print(f"PASS {'GPU' if gpu else 'CPU'} GUI repeated restart: both paused fields, camera/palettes/rate/seed settings, recovery rotation, graceful close")
