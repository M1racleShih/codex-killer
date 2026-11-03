#!/usr/bin/env python3
"""Regenerate committed icon PNGs from the SVG; requires rsvg-convert only for development."""
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SIZES = (16, 24, 32, 48, 64, 128, 256, 512)


def main():
    renderer = shutil.which('rsvg-convert')
    if not renderer:
        raise SystemExit('Install librsvg2-bin to regenerate icons. Normal builds use committed PNGs.')
    for size in SIZES:
        target = ROOT / 'packaging/icons' / str(size) / 'codex-killer.png'
        target.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([renderer, '--width', str(size), '--height', str(size),
                        '--output', str(target), str(ROOT / 'packaging/codex-killer.svg')], check=True)
        print(target.relative_to(ROOT))


if __name__ == '__main__':
    main()
