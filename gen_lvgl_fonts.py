#!/usr/bin/env python3
"""Generate LVGL fonts and normalize include path for ESP-IDF managed LVGL.

Reads `common_3500_chars.txt` (UTF-8): collapses all whitespace into one
`--symbols` string for `lv_font_conv` (Chinese / supplemental glyphs only).

Standard ASCII printable characters (0x20–0x7E), including space and symbols
that break Windows cmd parsing when passed in `--symbols` (&<>| etc.), are
added via `lv_font_conv`'s native `--range 0x20-0x7E` for `ui_font_custom.c`
only.
"""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parent
MAIN_DIR = PROJECT_ROOT / "main"
FONT_FILE = Path(r"C:\Windows\Fonts\simhei.ttf")
COMMON_3500_FILE = PROJECT_ROOT / "common_3500_chars.txt"


def normalize_lvgl_include(c_file: Path) -> None:
    """Force generated fonts to use include style compatible with this project."""
    text = c_file.read_text(encoding="utf-8")
    text = text.replace("\r\n", "\n")
    old_block = (
        '#ifdef LV_LVGL_H_INCLUDE_SIMPLE\n'
        '#include "lvgl.h"\n'
        '#else\n'
        '#include "lvgl/lvgl.h"\n'
        '#endif'
    )
    if old_block in text:
        text = text.replace(old_block, '#include "lvgl.h"', 1)
    else:
        text = text.replace('#include "lvgl/lvgl.h"', '#include "lvgl.h"')
    c_file.write_text(text, encoding="utf-8", newline="\n")


def read_symbols_file(path: Path) -> str:
    """Read UTF-8 file and collapse whitespace into a continuous symbols string."""
    raw = path.read_text(encoding="utf-8")
    symbols = "".join(ch for ch in raw if not ch.isspace())
    if not symbols:
        raise RuntimeError(f"Symbols file is empty after stripping whitespace: {path}")
    return symbols


def run_lv_font_conv(
    size: int,
    symbols: str,
    output_name: str,
    *,
    ascii_hex_range: str | None = None,
) -> None:
    lv_font_conv = shutil.which("lv_font_conv")
    if lv_font_conv is None:
        raise RuntimeError("lv_font_conv not found in PATH")

    out_path = MAIN_DIR / output_name
    cmd = [
        lv_font_conv,
        "--font",
        str(FONT_FILE),
        "--size",
        str(size),
        "--bpp",
        "1",
        "--format",
        "lvgl",
        "--no-compress",
    ]
    if ascii_hex_range:
        cmd.extend(["--range", ascii_hex_range])
    cmd.extend(
        [
            "--symbols",
            symbols,
            "-o",
            str(out_path),
        ]
    )
    subprocess.run(cmd, check=True, cwd=PROJECT_ROOT)
    normalize_lvgl_include(out_path)
    print(f"[OK] Generated {out_path.name}")


def main() -> int:
    symbols_str = read_symbols_file(COMMON_3500_FILE)

    run_lv_font_conv(
        size=20,
        symbols=symbols_str,
        output_name="ui_font_custom.c",
        ascii_hex_range="0x20-0x7E",
    )

    run_lv_font_conv(
        size=32,
        symbols="0123456789月日",
        output_name="ui_font_date_large.c",
    )

    print("All fonts generated and normalized for ESP-IDF LVGL.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
