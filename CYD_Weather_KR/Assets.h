#pragma once
// 자동 생성 파일: python tools/make_assets.py (직접 고치지 마세요)
// 원본: assets/ 폴더. 날씨 아이콘·달·바람 화살표(RLE), 상세 화면 큰 숫자 글꼴, 부팅 이미지
// 데이터 자체는 Assets.cpp 에 있습니다 (빌드 도구가 큰 배열을 여러 번 읽지 않게 분리).
#include <stdint.h>
#include <stddef.h>

struct AssetEntry { const char* name; uint32_t offset; };
constexpr int ASSET_COUNT = 63;
constexpr size_t ASSET_PACK_SIZE = 157829, FONT_NSBOLD36_SIZE = 6182;
extern const AssetEntry ASSET_INDEX[ASSET_COUNT];
extern const uint8_t ASSET_PACK[ASSET_PACK_SIZE];
extern const uint8_t FONT_NSBOLD36[FONT_NSBOLD36_SIZE];
