"""Fetch a project-local Windows CUDA 12.9.1 toolchain from NVIDIA redistributables.

No installer, driver, registry or PATH changes. Requires Python 3.9+; works from WSL.
Archives are checked against NVIDIA's SHA-256 manifest before extracting.
The selected release supports the project's MSVC v142 compiler and Blackwell GPU.
"""
from pathlib import Path, PurePosixPath
import hashlib
import json
import shutil
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1] / "x64" / "CudaToolkit"
BASE = "https://developer.download.nvidia.com/compute/cuda/redist/"
RELEASE = "12.9.1"
COMPONENTS = ("cuda_nvcc", "cuda_cudart", "cuda_cccl", "libcufft", "libnvjitlink")


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def main():
    ROOT.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(BASE + f"redistrib_{RELEASE}.json", timeout=60) as r:
        manifest = json.load(r)
    for name in COMPONENTS:
        entry = manifest[name]["windows-x86_64"]
        archive = ROOT / Path(entry["relative_path"]).name
        if not archive.exists() or digest(archive) != entry["sha256"]:
            print(f"Downloading {name}: {int(entry['size']) / 1e6:.1f} MB", flush=True)
            partial = archive.with_suffix(".partial")
            with urllib.request.urlopen(BASE + entry["relative_path"], timeout=120) as r:
                with partial.open("wb") as out:
                    shutil.copyfileobj(r, out)
            if digest(partial) != entry["sha256"]:
                raise RuntimeError(f"SHA-256 mismatch: {name}")
            partial.replace(archive)
        print(f"Extracting verified {name} {manifest[name]['version']}", flush=True)
        with zipfile.ZipFile(archive) as z:
            for item in z.infolist():
                parts = PurePosixPath(item.filename).parts[1:]
                if not parts or item.is_dir():
                    continue
                target = ROOT.joinpath(*parts).resolve()
                if not target.is_relative_to(ROOT.resolve()):
                    raise RuntimeError("Unsafe archive path")
                # Keep each component's license alongside its source metadata.
                if len(parts) == 1 and parts[0].lower().startswith(("license", "version")):
                    target = ROOT / "licenses" / name / parts[0]
                target.parent.mkdir(parents=True, exist_ok=True)
                with z.open(item) as source, target.open("wb") as dest:
                    shutil.copyfileobj(source, dest)
    (ROOT / "redistrib.json").write_text(json.dumps(manifest, indent=2))
    print(f"CUDA {RELEASE} ready at {ROOT}", flush=True)


if __name__ == "__main__":
    main()
