#!/usr/bin/env python3
"""실제 DisplayUI.cpp 를 PC 에서 컴파일해 화면 미리보기 PNG 를 만듭니다.

필요: clang++ (또는 g++), Pillow.   사용: python tools/preview/render.py [--cxx clang++]
결과: images/preview-*.png  (README 스크린샷), 글꼴에 없는 글자가 있으면 실패로 끝납니다.
"""
import argparse
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
BUILD = ROOT / ".build" / "preview"
OUT = ROOT / "images"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cxx", default=shutil.which("clang++") or shutil.which("g++") or "clang++")
    parser.add_argument("--scale", type=int, default=2)
    args = parser.parse_args()
    BUILD.mkdir(parents=True, exist_ok=True)
    exe = BUILD / ("preview.exe" if sys.platform == "win32" else "preview")
    cmd = [args.cxx, "-std=c++17", "-O1", "-w", f"-I{HERE / 'fake'}", "-D_CRT_SECURE_NO_WARNINGS",
           str(HERE / "preview.cpp"), str(HERE / "fake" / "fake_impl.cpp"), "-o", str(exe)]
    subprocess.run(cmd, check=True)
    result = subprocess.run([str(exe), str(ROOT / "CYD_Weather_KR" / "data"), str(BUILD)])
    OUT.mkdir(exist_ok=True)
    shots = sorted(BUILD.glob("*.ppm"))
    for ppm in shots:
        img = Image.open(ppm).convert("RGB")
        img = img.resize((img.width * args.scale, img.height * args.scale), Image.NEAREST)
        img.save(OUT / f"preview-{ppm.stem}.png")
    # 한 장으로 모아 보기 (README 상단용): 세로 4장 / 가로 4장
    for prefix, name in (("", "preview-all.png"), ("l-", "preview-all-landscape.png")):
        tiles = [Image.open(BUILD / f"{prefix}{n}.ppm").convert("RGB")
                 for n in ("0-home", "1-forecast", "2-details", "3-system") if (BUILD / f"{prefix}{n}.ppm").exists()]
        if not tiles:
            continue
        gap, tw, th = 16, tiles[0].width, tiles[0].height
        cols = 4 if not prefix else 2
        rows = (len(tiles) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * (tw + gap) + gap, rows * (th + gap) + gap), (24, 26, 30))
        for i, t in enumerate(tiles):
            sheet.paste(t, (gap + (i % cols) * (tw + gap), gap + (i // cols) * (th + gap)))
        sheet.save(OUT / name)
    print(f"PNG {len(shots)}장 → {OUT}")
    sys.exit(result.returncode)


if __name__ == "__main__":
    main()
