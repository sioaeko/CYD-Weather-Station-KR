#!/usr/bin/env python3
"""설정 웹 페이지(web/index.html)를 gzip 으로 압축해 펌웨어에 넣을 헤더로 만듭니다.

    python tools/build_web.py      → CYD_Weather_KR/WebPage.h

페이지를 고친 뒤에는 이 스크립트를 실행하고 펌웨어를 다시 올리면 됩니다.
"""
import gzip
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "web" / "index.html"
OUTPUT = ROOT / "CYD_Weather_KR" / "WebPage.h"


def main() -> None:
    raw = SOURCE.read_bytes()
    packed = gzip.compress(raw, compresslevel=9, mtime=0)  # mtime=0: 같은 입력이면 같은 출력
    rows = [", ".join(f"0x{b:02X}" for b in packed[i:i + 24]) for i in range(0, len(packed), 24)]
    OUTPUT.write_text(
        "#pragma once\n"
        "// 자동 생성 파일입니다. web/index.html 을 고친 뒤 `python tools/build_web.py` 를 실행하세요.\n"
        f"// 원본 {len(raw):,} 바이트 → gzip {len(packed):,} 바이트\n"
        "#include <stdint.h>\n#include <stddef.h>\n#ifndef PROGMEM\n#define PROGMEM\n#endif\n\n"
        "const uint8_t WEB_PAGE_GZ[] PROGMEM = {\n  " + ",\n  ".join(rows) + "\n};\n"
        f"const size_t WEB_PAGE_GZ_LEN = {len(packed)};\n",
        encoding="utf-8", newline="\n")
    print(f"web/index.html {len(raw) / 1024:.1f} KB → gzip {len(packed) / 1024:.1f} KB → {OUTPUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
