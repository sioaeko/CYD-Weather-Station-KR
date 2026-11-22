#!/usr/bin/env python3
"""TFT_eSPI 용 한글 스무스 폰트(.vlw)를 만들어 PROGMEM 헤더로 저장합니다.

한글 완성형 11,172자를 모두 넣으면 플래시가 모자라므로, 실제로 화면에 쓰는 글자만 고릅니다.
  1) 스케치 폴더의 .ino/.h/.cpp 에 있는 "문자열 리터럴" 속 글자 (주석은 제외)
  2) tools/extra_glyphs.txt 의 글자 (전국 시·군·구 이름 등, 지역을 바꿔도 대부분 그대로 표시)
  3) 출력 가능한 ASCII 전체와 몇 가지 기호

사용법 (All_Settings.h 에 새 한글 지명을 넣었을 때만 다시 실행하면 됩니다):
    pip install pillow
    python tools/make_korean_fonts.py                 # 시스템의 나눔고딕 Bold 자동 탐색
    python tools/make_korean_fonts.py --font 경로.ttf  # 다른 TTF/OTF 사용

출력: CYD_Weather_KR/KoreanFonts.h
  KR_FONT_SMALL  12px  – 전체 글자
  KR_FONT_MEDIUM 16px  – 전체 글자
  KR_FONT_LARGE  22px  – 날씨 설명·날짜·지역 이름에 쓰는 글자만 (WeatherModel.cpp, All_Settings.h)
  KR_FONT_CLOCK  46px  – 시계용 숫자와 콜론만 (세로 화면)
  KR_FONT_CLOCK_BIG 66px – 시계용 숫자와 콜론만 (가로 화면, 침대에서도 잘 보이게)
"""
from __future__ import annotations

import argparse
import os
import struct
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:  # pragma: no cover
    sys.exit("Pillow 가 필요합니다:  pip install pillow")

ROOT = Path(__file__).resolve().parent.parent
SKETCH = ROOT / "CYD_Weather_KR"
OUTPUT = SKETCH / "KoreanFonts.h"
EXTRA = Path(__file__).resolve().parent / "extra_glyphs.txt"
SYMBOLS = "°·㎍㎥…□↑↓←→"
FONT_CANDIDATES = [
    Path(__file__).resolve().parent / "fonts" / "NanumGothicBold.ttf",
    Path(__file__).resolve().parent / "fonts" / "NanumGothic-Bold.ttf",
    Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts" / "NanumGothicBold.ttf",
    Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts" / "NanumGothic-Bold.ttf",
    Path(os.environ.get("LOCALAPPDATA", "")) / "Microsoft/Windows/Fonts/NanumGothicBold.ttf",
    Path(os.environ.get("LOCALAPPDATA", "")) / "Microsoft/Windows/Fonts/NanumGothic-Bold.ttf",
    Path("/Library/Fonts/NanumGothicBold.ttf"),
    Path.home() / "Library/Fonts/NanumGothicBold.ttf",
    Path("/usr/share/fonts/truetype/nanum/NanumGothicBold.ttf"),
    Path("/usr/share/fonts/nanum/NanumGothicBold.ttf"),
]


def string_literals(source: str) -> list[str]:
    """C/C++ 소스에서 주석을 건너뛰고 "..." 문자열 리터럴의 내용만 모읍니다."""
    out, i, n = [], 0, len(source)
    while i < n:
        c = source[i]
        if source.startswith("//", i):
            j = source.find("\n", i)
            i = n if j < 0 else j
        elif source.startswith("/*", i):
            j = source.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif c == "'":
            i += 1
            while i < n and source[i] != "'":
                i += 2 if source[i] == "\\" else 1
            i += 1
        elif c == '"':
            # 원시 문자열 R"PEM(...)PEM" 은 인증서라 글자 수집에서 제외
            if i > 0 and source[i - 1] == "R":
                delim_end = source.find("(", i)
                tag = source[i + 1:delim_end]
                j = source.find(")" + tag + '"', delim_end)
                i = n if j < 0 else j + len(tag) + 2
                continue
            i += 1
            start = i
            while i < n and source[i] != '"' and source[i] != "\n":
                i += 2 if source[i] == "\\" else 1
            out.append(source[start:i])
            i += 1
        else:
            i += 1
    return out


LARGE_SOURCES = {"WeatherModel.cpp", "All_Settings.h"}
CLOCK_CHARS = "0123456789:-"
# 큰 글꼴로 그리는 DisplayUI.cpp 의 문구 (요일·연결 상태)
LARGE_EXTRA = "월일요 날씨 받는 중 Wi-Fi 연결 오전 오후"


def collect_characters() -> tuple[str, str, dict[str, int]]:
    chars = {chr(c) for c in range(0x21, 0x7F)} | set(SYMBOLS)
    large = {chr(c) for c in range(0x21, 0x7F)} | set(SYMBOLS) | set(LARGE_EXTRA)
    stats = {}
    for path in sorted(SKETCH.glob("*")):
        if path.suffix not in {".ino", ".h", ".cpp"} or path.name in {"KoreanFonts.h", "Secrets.h"}:
            continue
        found = {ch for lit in string_literals(path.read_text(encoding="utf-8")) for ch in lit if ord(ch) >= 0x80}
        stats[path.name] = len(found)
        chars |= found
        if path.name in LARGE_SOURCES:
            large |= found
    if EXTRA.exists():
        for line in EXTRA.read_text(encoding="utf-8").splitlines():
            if not line.startswith("#"):
                chars |= {ch for ch in line if ord(ch) > 0x20 and not ch.isspace()}
    chars = {ch for ch in chars if ord(ch) <= 0xFFFF}  # TFT_eSPI 는 BMP(16비트) 코드만 지원
    large = {ch for ch in large if ord(ch) <= 0xFFFF}
    return "".join(sorted(chars)), "".join(sorted(large)), stats


def build_vlw(font_path: Path, size: int, chars: str) -> tuple[bytes, list[str]]:
    font = ImageFont.truetype(str(font_path), size)
    glyphs, missing = [], []
    try:  # fontTools 가 있으면 글꼴에 실제로 있는 글자인지 확인 (없으면 □ 모양이 들어감)
        from fontTools.ttLib import TTFont
        cmap = TTFont(str(font_path), fontNumber=0).getBestCmap()
    except Exception:  # noqa: BLE001
        cmap = None
    for ch in chars:
        if cmap is not None and ord(ch) not in cmap:
            missing.append(ch)
            continue
        x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
        advance = round(font.getlength(ch))
        if x1 <= x0 or y1 <= y0:
            missing.append(ch)
            continue
        image = Image.new("L", (x1 - x0, y1 - y0), 0)
        ImageDraw.Draw(image).text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
        glyphs.append((ord(ch), image.width, image.height, advance, -y0, x0, image.tobytes()))
    # 행 높이는 한글·영문 글자의 실제 잉크 범위로 계산 (TFT_eSPI 는 ascent 를 윗기준선으로 씀)
    ascent = max(g[4] for g in glyphs)
    descent = max(g[2] - g[4] for g in glyphs)
    header = struct.pack(">6i", len(glyphs), 11, size, 0, ascent, descent)
    metrics = b"".join(struct.pack(">7i", u, h, w, adv, dy, dx, 0) for u, w, h, adv, dy, dx, _ in glyphs)
    bitmaps = b"".join(g[6] for g in glyphs)
    for u, w, h, adv, dy, dx, _ in glyphs:
        if w > 255 or h > 255 or adv > 255 or not -128 <= dx <= 127:
            raise SystemExit(f"글리프 U+{u:04X} 크기가 TFT_eSPI 한계를 넘습니다")
    return header + metrics + bitmaps, missing


def c_array(name: str, data: bytes) -> str:
    rows = [", ".join(f"0x{b:02X}" for b in data[i:i + 20]) for i in range(0, len(data), 20)]
    return f"const uint8_t {name}[] PROGMEM = {{\n  " + ",\n  ".join(rows) + "\n};\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--font", type=Path, help="TTF/OTF 경로 (기본: 나눔고딕 Bold 자동 탐색)")
    parser.add_argument("--small", type=int, default=12, help="작은 글꼴 픽셀 크기 (기본 12)")
    parser.add_argument("--medium", type=int, default=16, help="중간 글꼴 픽셀 크기 (기본 16)")
    parser.add_argument("--large", type=int, default=22, help="큰 글꼴 픽셀 크기 (기본 22)")
    parser.add_argument("--clock", type=int, default=46, help="세로 화면 시계 숫자 픽셀 크기 (기본 46)")
    parser.add_argument("--clock-big", type=int, default=66, help="가로 화면 시계 숫자 픽셀 크기 (기본 66)")
    parser.add_argument("--vlw-dir", type=Path, help="확인용 .vlw 파일도 이 폴더에 저장")
    args = parser.parse_args()

    font_path = args.font or next((p for p in FONT_CANDIDATES if p.is_file()), None)
    if not font_path or not font_path.is_file():
        sys.exit("나눔고딕 Bold(NanumGothicBold.ttf)를 찾지 못했습니다. --font 로 경로를 지정하거나 "
                 "tools/fonts/ 폴더에 넣어 주세요. (https://hangeul.naver.com/font)")

    chars, large_chars, stats = collect_characters()
    hangul = sum(1 for ch in chars if "\uac00" <= ch <= "\ud7a3")
    print(f"폰트: {font_path}")
    print(f"글자 수: {len(chars)} (한글 {hangul}자)  ← " + ", ".join(f"{k} {v}" for k, v in stats.items()))

    parts = [
        "#pragma once\n",
        "// 자동 생성 파일입니다. 직접 고치지 말고 `python tools/make_korean_fonts.py` 를 실행하세요.\n",
        f"// 원본 글꼴: {font_path.name} (SIL Open Font License 1.1, THIRD_PARTY_NOTICES.txt 참고)\n",
        f"// 글자 {len(chars)}개 (한글 {hangul}자)\n",
        "#include <stdint.h>\n#ifndef PROGMEM\n#define PROGMEM\n#endif\n\n",
    ]
    total = 0
    fonts = (("KR_FONT_SMALL", args.small, chars), ("KR_FONT_MEDIUM", args.medium, chars),
             ("KR_FONT_LARGE", args.large, large_chars), ("KR_FONT_CLOCK", args.clock, CLOCK_CHARS),
             ("KR_FONT_CLOCK_BIG", args.clock_big, CLOCK_CHARS))
    for name, size, charset in fonts:
        data, missing = build_vlw(font_path, size, charset)
        total += len(data)
        if missing:
            print(f"  {name}: 글꼴에 없는 글자 {len(missing)}개 건너뜀: {''.join(missing)[:40]}")
        print(f"  {name}: {size}px, 글자 {len(charset) - len(missing)}개, {len(data) / 1024:.1f} KB")
        parts.append(f"// {size}px\n" + c_array(name, data) + "\n")
        if args.vlw_dir:
            args.vlw_dir.mkdir(parents=True, exist_ok=True)
            (args.vlw_dir / f"{name}.vlw").write_bytes(data)
    OUTPUT.write_text("".join(parts), encoding="utf-8", newline="\n")
    print(f"저장: {OUTPUT.relative_to(ROOT)}  (플래시 사용량 약 {total / 1024:.0f} KB)")


if __name__ == "__main__":
    main()
