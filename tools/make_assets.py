#!/usr/bin/env python3
"""assets/ 의 그림·글꼴을 펌웨어 안에 넣을 CYD_Weather_KR/Assets.h 로 만듭니다.

예전에는 LittleFS 에 따로 올렸지만, Wi-Fi 업데이트(OTA) 한 번으로 그림까지 같이 바뀌도록 펌웨어에 넣습니다.
24비트 BMP 는 화면 색(RGB565)으로 바꾼 뒤 줄 단위 RLE 로 압축합니다 (손실 없음, 약 890KB → 130KB).

형식 (그림 하나):
  [폭 1바이트][높이 1바이트] 다음 줄마다 [이 줄의 바이트 수 2바이트 LE][런...]
  런: 첫 바이트 n 이 0x80 이상이면 다음 색(2바이트 LE)을 (n & 0x7F) + 1 번 반복,
      0x80 미만이면 다음 n + 1 개의 색이 그대로 나옴.  색 0(검정)은 투명으로 그리지 않음.

사용법:  python tools/make_assets.py
"""
from __future__ import annotations

import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"
OUTPUT = ROOT / "CYD_Weather_KR" / "Assets.h"
SOURCE = ROOT / "CYD_Weather_KR" / "Assets.cpp"
IMAGE_DIRS = ["icon", "icon50", "moon", "wind"]


def load_bmp(path: Path) -> tuple[int, int, list[list[int]]]:
    b = path.read_bytes()
    if b[:2] != b"BM":
        raise SystemExit(f"BMP 아님: {path}")
    offset = struct.unpack("<I", b[10:14])[0]
    w, h = struct.unpack("<ii", b[18:26])
    bpp = struct.unpack("<H", b[28:30])[0]
    if bpp != 24 or not (0 < w <= 120 and 0 < h <= 120):
        raise SystemExit(f"24비트 120px 이하 BMP 만 됩니다: {path}")
    stride = (w * 3 + 3) & ~3
    rows = []
    for y in range(h):
        r = b[offset + (h - 1 - y) * stride: offset + (h - 1 - y) * stride + w * 3]
        rows.append([((r[i * 3 + 2] & 0xF8) << 8) | ((r[i * 3 + 1] & 0xFC) << 3) | (r[i * 3] >> 3) for i in range(w)])
    return w, h, rows


def rle_row(row: list[int]) -> bytes:
    out = bytearray()
    i, n = 0, len(row)
    while i < n:
        j = i
        while j < n and row[j] == row[i] and j - i < 128:
            j += 1
        if j - i >= 2:
            out += bytes([0x80 | (j - i - 1)]) + struct.pack("<H", row[i])
            i = j
            continue
        k = i
        while k < n and k - i < 128 and not (k + 1 < n and row[k + 1] == row[k]):
            k += 1
        k = max(k, i + 1)
        out += bytes([k - i - 1]) + b"".join(struct.pack("<H", c) for c in row[i:k])
        i = k
    return bytes(out)


def load_jpg(path: Path) -> tuple[int, int, list[list[int]]]:
    from PIL import Image  # pip install pillow
    im = Image.open(path).convert("RGB")
    if im.width > 240 or im.height > 240:
        raise SystemExit(f"240px 이하 그림만 됩니다: {path}")
    px = im.load()
    rows = [[((px[x, y][0] & 0xF8) << 8) | ((px[x, y][1] & 0xFC) << 3) | (px[x, y][2] >> 3) for x in range(im.width)]
            for y in range(im.height)]
    return im.width, im.height, rows


def encode(path: Path) -> bytes:
    w, h, rows = load_jpg(path) if path.suffix.lower() == ".jpg" else load_bmp(path)
    out = bytearray([w, h])
    for row in rows:
        data = rle_row(row)
        out += struct.pack("<H", len(data)) + data
    return bytes(out)


def c_bytes(data: bytes) -> str:
    lines = []
    for i in range(0, len(data), 24):
        lines.append("  " + ",".join(f"0x{x:02x}" for x in data[i:i + 24]) + ",")
    return "\n".join(lines)


def main() -> None:
    pack = bytearray()
    entries: list[tuple[str, int]] = []
    raw_total = 0
    for d in IMAGE_DIRS:
        for f in sorted((ASSETS / d).glob("*.bmp")):
            entries.append((f"/{d}/{f.name}", len(pack)))
            pack += encode(f)
            raw_total += f.stat().st_size
    # 부팅 이미지도 같은 RLE 로 (JPEG 디코더 라이브러리를 빼려고). 이름은 /splash/OpenWeather
    splash_path = ASSETS / "splash" / "OpenWeather.jpg"
    entries.append(("/splash/OpenWeather", len(pack)))
    pack += encode(splash_path)
    font = (ASSETS / "fonts" / "NSBold36.vlw").read_bytes()

    header = (
        "#pragma once\n"
        "// 자동 생성 파일: python tools/make_assets.py (직접 고치지 마세요)\n"
        "// 원본: assets/ 폴더. 날씨 아이콘·달·바람 화살표(RLE), 상세 화면 큰 숫자 글꼴, 부팅 이미지\n"
        "// 데이터 자체는 Assets.cpp 에 있습니다 (빌드 도구가 큰 배열을 여러 번 읽지 않게 분리).\n"
        "#include <stdint.h>\n#include <stddef.h>\n\n"
        "struct AssetEntry { const char* name; uint32_t offset; };\n"
        f"constexpr int ASSET_COUNT = {len(entries)};\n"
        f"constexpr size_t ASSET_PACK_SIZE = {len(pack)}, FONT_NSBOLD36_SIZE = {len(font)};\n"
        "extern const AssetEntry ASSET_INDEX[ASSET_COUNT];\n"
        "extern const uint8_t ASSET_PACK[ASSET_PACK_SIZE];\n"
        "extern const uint8_t FONT_NSBOLD36[FONT_NSBOLD36_SIZE];\n"
    )
    index = "".join(f'  {{"{n}", {o}}},\n' for n, o in entries)
    source = (
        "// 자동 생성 파일: python tools/make_assets.py (직접 고치지 마세요)\n"
        '#include "Assets.h"\n\n'
        f"const AssetEntry ASSET_INDEX[ASSET_COUNT] = {{\n{index}}};\n"
        f"const uint8_t ASSET_PACK[ASSET_PACK_SIZE] = {{\n{c_bytes(bytes(pack))}\n}};\n"
        f"const uint8_t FONT_NSBOLD36[FONT_NSBOLD36_SIZE] = {{\n{c_bytes(font)}\n}};\n"
    )
    OUTPUT.write_text(header, encoding="utf-8", newline="\n")
    SOURCE.write_text(source, encoding="utf-8", newline="\n")
    print(f"그림 {len(entries)}개(부팅 이미지 포함): BMP {raw_total / 1024:.0f}KB → 묶음 {len(pack) / 1024:.0f}KB, "
          f"글꼴 {len(font) / 1024:.1f}KB → {SOURCE.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
