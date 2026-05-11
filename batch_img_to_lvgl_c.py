#!/usr/bin/env python3
"""
Batch convert 64x64 PNG icons into LVGL v9 I1 C arrays.

Input folder : ./icons_png
Output folder: ./ui_assets

Rules implemented:
- Strictly 64x64 image size.
- Strict binary threshold: grayscale < 128 => black bit(1), else white bit(0).
- Transparent pixels are always background bit(0).
- Bit-packed 1bpp output, exactly 8 bytes/row, 512 bytes/image.
- Chinese filename support via pypinyin transliteration and safe C identifiers.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path
from typing import Dict, List, Sequence, Set

try:
    from PIL import Image
except ImportError as exc:
    raise SystemExit("Missing dependency: pillow. Install with: pip install pillow") from exc

if hasattr(Image, "Resampling"):
    RESAMPLE_NEAREST = Image.Resampling.NEAREST
else:
    RESAMPLE_NEAREST = Image.NEAREST

try:
    from pypinyin import lazy_pinyin
except ImportError:
    lazy_pinyin = None


INPUT_DIR = Path("icons_png")
OUTPUT_DIR = Path("ui_assets")
ICON_W = 64
ICON_H = 64
LARGE_ICON_W = 96
LARGE_ICON_H = 96
PALETTE_SIZE = 8

# Preferred fixed English aliases for known icon meanings.
# Keys support both Chinese and pinyin-ish source names.
ENGLISH_ALIAS_MAP: Dict[str, str] = {
    "笔记": "note",
    "biji": "note",
    "阅读": "read",
    "yuedu": "read",
    "相册": "photo",
    "xiangce": "photo",
    "单词库": "word",
    "danciku": "word",
    "音乐": "music",
    "yinle": "music",
}


# Minimal fallback mapping if pypinyin is unavailable.
FALLBACK_PINYIN_MAP: Dict[str, str] = {
    "笔": "bi",
    "记": "ji",
    "设": "she",
    "置": "zhi",
    "菜": "cai",
    "单": "dan",
    "时": "shi",
    "钟": "zhong",
    "闹": "nao",
    "铃": "ling",
    "音": "yin",
    "乐": "yue",
}


def to_identifier_core(raw_name: str) -> str:
    """Convert raw filename stem to a safe identifier core."""
    normalized_name = re.sub(r"[\s\-_]+", "", raw_name).lower()
    if normalized_name in ENGLISH_ALIAS_MAP:
        return ENGLISH_ALIAS_MAP[normalized_name]

    parts: List[str] = []
    for ch in raw_name:
        if ch.isascii() and (ch.isalnum() or ch == "_"):
            parts.append(ch.lower())
            continue

        if "\u4e00" <= ch <= "\u9fff":
            if lazy_pinyin is not None:
                py_list = lazy_pinyin(ch, errors="ignore")
                py = py_list[0] if py_list else ""
            else:
                py = FALLBACK_PINYIN_MAP.get(ch, "")

            if py:
                parts.append(py.lower())
            else:
                parts.append(f"u{ord(ch):x}")
            continue

        # Other non-ascii symbols become separator.
        parts.append("_")

    core = "".join(parts)
    core = re.sub(r"_+", "_", core).strip("_")
    core = re.sub(r"[^a-z0-9_]", "_", core)

    if not core:
        core = "icon"
    if not (core[0].isalpha() or core[0] == "_"):
        core = f"n_{core}"
    return core


def ensure_unique_name(base_name: str, used: Set[str]) -> str:
    """Ensure generated C symbol name is globally unique."""
    if base_name not in used:
        used.add(base_name)
        return base_name

    idx = 2
    while True:
        candidate = f"{base_name}_{idx}"
        if candidate not in used:
            used.add(candidate)
            return candidate
        idx += 1


def pack_rgba_to_i1_payload(img: Image.Image, width: int, height: int) -> bytes:
    """Convert RGBA image to packed I1 payload."""
    if img.size != (width, height):
        raise ValueError(f"Image size must be {width}x{height}, got {img.size[0]}x{img.size[1]}")
    if (width % 8) != 0:
        raise ValueError(f"Width must be multiple of 8 for I1 bit packing, got {width}")

    row_bytes = width // 8
    payload_size = row_bytes * height
    rgba = img.convert("RGBA")
    px = rgba.load()
    packed = bytearray(payload_size)

    out_idx = 0
    for y in range(height):
        for byte_i in range(row_bytes):
            b = 0
            for bit_i in range(8):
                x = byte_i * 8 + bit_i
                r, g, bb, a = px[x, y]

                if a == 0:
                    bit = 0
                else:
                    gray = (299 * r + 587 * g + 114 * bb) // 1000
                    bit = 1 if gray < 128 else 0

                # High bit is left-most pixel in each 8-pixel group.
                if bit:
                    b |= 1 << (7 - bit_i)

            packed[out_idx] = b
            out_idx += 1

    return bytes(packed)


def with_lvgl_i1_palette(pixel_payload: bytes) -> bytes:
    """
    Prepend LVGL v9 I1 required palette:
    index 0 (bg/bit0): white ARGB8888 => FF FF FF FF
    index 1 (fg/bit1): black ARGB8888 => 00 00 00 FF
    """
    palette = bytes(
        [
            0xFF, 0xFF, 0xFF, 0xFF,  # index 0: white
            0x00, 0x00, 0x00, 0xFF,  # index 1: black
        ]
    )
    return palette + pixel_payload


def format_c_array(data: bytes, bytes_per_line: int = 16) -> str:
    rows: List[str] = []
    for i in range(0, len(data), bytes_per_line):
        chunk = data[i : i + bytes_per_line]
        rows.append("    " + ", ".join(f"0x{v:02X}" for v in chunk))
    return ",\n".join(rows)


def generate_c_source(symbol: str, width: int, height: int, data: bytes) -> str:
    c_array_name = f"{symbol}_map"
    data_size = len(data)
    array_text = format_c_array(data)

    return f"""#include "lvgl.h"

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_LARGE_CONST
#define LV_ATTRIBUTE_LARGE_CONST
#endif

LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST
const uint8_t {c_array_name}[{data_size}] = {{
{array_text}
}};

const lv_img_dsc_t {symbol} = {{
    .header = {{
        .cf = LV_COLOR_FORMAT_I1,
        .w = {width},
        .h = {height}
    }},
    .data_size = {data_size},
    .data = {c_array_name}
}};
"""


def iter_png_files(path: Path) -> Sequence[Path]:
    return sorted(path.glob("*.png"))


def convert_all(input_dir: Path, output_dir: Path) -> int:
    if not input_dir.exists() or not input_dir.is_dir():
        raise FileNotFoundError(f"Input folder not found: {input_dir}")

    output_dir.mkdir(parents=True, exist_ok=True)
    png_files = iter_png_files(input_dir)
    if not png_files:
        print(f"[WARN] No .png files found in: {input_dir}")
        return 0

    used_symbols: Set[str] = set()
    success = 0

    for src in png_files:
        stem = src.stem
        core = to_identifier_core(stem)
        symbol = ensure_unique_name(f"ui_icon_{core}", used_symbols)
        large_symbol = f"{symbol}_large"
        dst_normal = output_dir / f"{symbol}.c"
        dst_large = output_dir / f"{large_symbol}.c"

        try:
            with Image.open(src) as img:
                if img.size != (ICON_W, ICON_H):
                    raise ValueError(f"Image size must be {ICON_W}x{ICON_H}, got {img.size[0]}x{img.size[1]}")

                normal_pixels = pack_rgba_to_i1_payload(img, ICON_W, ICON_H)
                normal_payload = with_lvgl_i1_palette(normal_pixels)
                normal_expected_size = PALETTE_SIZE + (ICON_H * (ICON_W // 8))
                if len(normal_payload) != normal_expected_size:
                    raise ValueError(
                        f"Normal payload size mismatch: {len(normal_payload)} != {normal_expected_size}"
                    )

                large_img = img.resize((LARGE_ICON_W, LARGE_ICON_H), RESAMPLE_NEAREST)
                large_pixels = pack_rgba_to_i1_payload(large_img, LARGE_ICON_W, LARGE_ICON_H)
                large_payload = with_lvgl_i1_palette(large_pixels)
                large_expected_size = PALETTE_SIZE + (LARGE_ICON_H * (LARGE_ICON_W // 8))
                if len(large_payload) != large_expected_size:
                    raise ValueError(
                        f"Large payload size mismatch: {len(large_payload)} != {large_expected_size}"
                    )

            normal_c_text = generate_c_source(symbol, ICON_W, ICON_H, normal_payload)
            dst_normal.write_text(normal_c_text, encoding="utf-8", newline="\n")
            print(f"[OK] {src.name} -> {dst_normal.name} ({len(normal_payload)} bytes)")
            success += 1

            large_c_text = generate_c_source(large_symbol, LARGE_ICON_W, LARGE_ICON_H, large_payload)
            dst_large.write_text(large_c_text, encoding="utf-8", newline="\n")
            print(f"[OK] {src.name} -> {dst_large.name} ({len(large_payload)} bytes)")
            success += 1
        except Exception as exc:  # Keep processing next files.
            print(f"[ERR] {src.name}: {exc}", file=sys.stderr)

    return success


def main() -> int:
    print("== batch_img_to_lvgl_c ==")
    print(f"Input : {INPUT_DIR.resolve()}")
    print(f"Output: {OUTPUT_DIR.resolve()}")

    if lazy_pinyin is None:
        print("[WARN] pypinyin not installed. Using limited fallback mapping.")
        print("       Recommended: pip install pypinyin")

    count = convert_all(INPUT_DIR, OUTPUT_DIR)
    print(f"Done. Converted {count} file(s).")
    return 0 if count >= 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
