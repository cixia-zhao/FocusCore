#!/usr/bin/env python3
"""Industrial Markdown -> LVGL v9 .bin conversion pipeline.

Semantic slicing: splits each .md at H2/H3 boundaries (not by pixel height),
renders each sub-section as an independent .bin, and emits a tree-structured
index.json for hierarchical menu navigation on the ESP32.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import struct
import sys
import time
from collections import OrderedDict
from pathlib import Path

import markdown
from PIL import Image
from playwright.sync_api import sync_playwright
from urllib.parse import unquote

TARGET_WIDTH = 400
MAX_PAGE_PX = 8000            # physical fallback slice (rare)
SECTION_HASH_LEN = 12         # chars of MD5 used as .bin filename
MAGIC = 0x19
COLOR_FORMAT_L8 = 0x06
FLAGS = 0x00

CACHE_FILE = "_sync_cache.json"
INDEX_FILE = "index.json"


# ── Semantic section parser ─────────────────────────────────────────────────

def resolve_images(text: str, md_path: Path) -> str:
    """Resolve both Obsidian ![[wikilink]] and standard ![alt](relpath) images
    to absolute file:// URIs within the given text snippet."""
    def _obsidian(match: re.Match[str]) -> str:
        name = match.group(1)
        parents = md_path.parents
        root = parents[3] if len(parents) > 3 else md_path.parent
        found = next(root.rglob(name), None)
        if found is not None:
            return f"![]({found.absolute().as_uri()})"
        return f'![]({name.replace(" ", "%20")})'

    def _standard(match: re.Match[str]) -> str:
        alt, raw = match.group(1), match.group(2)
        rel = unquote(raw)
        img = (md_path.parent / rel).resolve()
        if img.is_file():
            return f"![{alt}]({img.as_uri()})"
        return match.group(0)

    text = re.sub(r"!\[\[(.*?)\]\]", _obsidian, text)
    text = re.sub(r"!\[(.*?)\]\((.*?)\)", _standard, text)
    return text


def parse_markdown_sections(md_text: str) -> list:
    """Line-by-line state machine: 4-level (H1→H2→H3→content).

    Returns list of (h1_title, [(h2_title, [(h3_title_or_empty, content_md), ...]), ...])

    Missing H1 defaults to ``__DEFAULT__``; missing H2 defaults to ``__DEFAULT__``;
    content before any ``###`` carries ``正文`` as its H3 placeholder.
    All heading lines are stripped from rendered content.
    """
    current_h1 = "__DEFAULT__"
    current_h2 = "__DEFAULT__"
    current_h3 = "正文"
    current_content: list[str] = []

    sections: OrderedDict[str, OrderedDict[str, list[tuple[str, str]]]] = OrderedDict()

    def flush() -> None:
        content = "\n".join(current_content).strip()
        if content:
            if current_h1 not in sections:
                sections[current_h1] = OrderedDict()
            if current_h2 not in sections[current_h1]:
                sections[current_h1][current_h2] = []
            sections[current_h1][current_h2].append((current_h3, content))
        current_content.clear()

    for line in md_text.split("\n"):
        m1 = re.match(r"^# (.+)$", line)
        if m1:
            flush()
            current_h1 = m1.group(1).strip()
            current_h2 = "__DEFAULT__"
            current_h3 = "正文"
            continue

        m2 = re.match(r"^## (.+)$", line)
        if m2:
            flush()
            current_h2 = m2.group(1).strip()
            current_h3 = "正文"
            continue

        m3 = re.match(r"^### (.+)$", line)
        if m3:
            flush()
            current_h3 = m3.group(1).strip()
            continue

        current_content.append(line)

    flush()

    return [(h1, [(h2, entries) for h2, entries in h2_dict.items()])
            for h1, h2_dict in sections.items()]


def content_stem(content: str) -> str:
    """Deterministic hash-based filename stem (safe for FATFS)."""
    return hashlib.md5(content.encode("utf-8")).hexdigest()[:SECTION_HASH_LEN]


# ── Rendering pipeline (preserved) ──────────────────────────────────────────

def render_markdown_to_html(markdown_text: str) -> str:
    """Convert markdown text to full HTML document."""
    html_body = markdown.markdown(
        markdown_text,
        extensions=[
            "fenced_code",
            "tables",
            "attr_list",
            "nl2br",
            "pymdownx.arithmatex",
        ],
        extension_configs={
            "pymdownx.arithmatex": {"generic": True},
        },
    )

    return f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <style>
    body {{
      width: 400px;
      margin: 0;
      padding: 15px;
      box-sizing: border-box;
      background: white;
      color: black;
      font-size: 20px;
      font-family: sans-serif;
      line-height: 1.5;
      overflow-x: hidden;
      overflow-wrap: break-word;
    }}
    pre {{
      overflow-x: auto;
      background: #f5f5f5;
      padding: 10px;
      border-radius: 6px;
    }}
    code {{
      font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
    }}
    table {{
      border-collapse: collapse;
      width: 100%;
    }}
    th, td {{
      border: 1px solid #999;
      padding: 6px;
      text-align: left;
      vertical-align: top;
    }}
    img {{
      max-width: 100%;
      height: auto;
      display: block;
    }}
  </style>
  <link
    rel="stylesheet"
    href="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/styles/default.min.css"
  />
  <script>
    window.mathjax_done = false;
    window.MathJax = {{
      tex: {{
        inlineMath: [['$', '$'], ['\\\\(', '\\\\)']],
        displayMath: [['$$', '$$'], ['\\\\[', '\\\\]']]
      }},
      startup: {{
        ready: () => {{
          MathJax.startup.defaultReady();
          MathJax.startup.promise.then(() => {{
            window.mathjax_done = true;
          }}).catch((err) => {{
            console.error("MathJax Error:", err);
            window.mathjax_done = true;
          }});
        }}
      }}
    }};
  </script>
  <script defer src="https://cdn.jsdelivr.net/npm/mathjax@3/es5/tex-mml-chtml.js"></script>
  <script defer src="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/highlight.min.js"></script>
  <script>
    window.addEventListener('load', () => {{
      if (window.hljs) {{
        hljs.highlightAll();
      }}
    }});
  </script>
</head>
<body>
{html_body}
</body>
</html>
"""


def wait_for_layout_stable(page, rounds: int = 4, interval: float = 0.25) -> None:
    """Wait until page scroll height remains stable."""
    stable_hits = 0
    last_height = -1
    deadline = time.time() + 10.0
    while time.time() < deadline:
        height = page.evaluate("() => document.documentElement.scrollHeight")
        if height == last_height:
            stable_hits += 1
            if stable_hits >= rounds:
                return
        else:
            stable_hits = 0
            last_height = height
        time.sleep(interval)


def capture_full_page(temp_html: Path, output_png: Path) -> None:
    """Open temp HTML in Playwright and screenshot the full page."""
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(headless=True)
        page = browser.new_page(viewport={"width": TARGET_WIDTH, "height": 800})
        try:
            page.goto(temp_html.resolve().as_uri(), wait_until="domcontentloaded", timeout=60000)
            page.wait_for_load_state("networkidle", timeout=60000)
            page.wait_for_function("() => window.mathjax_done === true", timeout=90000)
            wait_for_layout_stable(page)
            page.screenshot(path=str(output_png), full_page=True)
        finally:
            browser.close()


def normalize_image_to_l8(input_png: Path) -> Image.Image:
    """Open screenshot, enforce 400px width with crop/pad only, convert to L."""
    image = Image.open(input_png)
    width, height = image.size

    if width > TARGET_WIDTH:
        image = image.crop((0, 0, TARGET_WIDTH, height))
    elif width < TARGET_WIDTH:
        padded = Image.new("RGB", (TARGET_WIDTH, height), "white")
        x_offset = (TARGET_WIDTH - width) // 2
        padded.paste(image, (x_offset, 0))
        image = padded

    if image.mode != "L":
        image = image.convert("L")

    return image


def pack_lvgl_v9_l8(image_l8: Image.Image) -> bytes:
    """Pack image to LVGL v9 binary format with 12-byte header."""
    width, height = image_l8.size
    if width != TARGET_WIDTH:
        raise ValueError(f"Width must be {TARGET_WIDTH}, got {width}")

    stride = TARGET_WIDTH
    pixel_data = image_l8.tobytes()

    header = struct.pack(
        "<BBBHHH3x",
        MAGIC,
        COLOR_FORMAT_L8,
        FLAGS,
        width,
        height,
        stride,
    )

    return header + pixel_data


# ── Slicing (physical fallback) ─────────────────────────────────────────────

def slice_and_pack(image_l8: Image.Image, stem_root: str, output_dir: Path) -> list[Path]:
    """Physical slice fallback: split tall image by MAX_PAGE_PX.

    Returns list[Path]; single-element for most sections (now semantically small).
    """
    width, height = image_l8.size
    assert width == TARGET_WIDTH

    if height <= MAX_PAGE_PX:
        out = output_dir / f"{stem_root}.bin"
        out.write_bytes(pack_lvgl_v9_l8(image_l8))
        return [out]

    paths: list[Path] = []
    page = 1
    for y in range(0, height, MAX_PAGE_PX):
        chunk_h = min(MAX_PAGE_PX, height - y)
        chunk = image_l8.crop((0, y, TARGET_WIDTH, y + chunk_h))
        out = output_dir / f"{stem_root}_p{page}.bin"
        out.write_bytes(pack_lvgl_v9_l8(chunk))
        paths.append(out)
        page += 1
    return paths


# ── Incremental cache ──────────────────────────────────────────────────────

def _cache_path(output_dir: Path) -> Path:
    return output_dir / CACHE_FILE


def load_cache(output_dir: Path) -> dict:
    cp = _cache_path(output_dir)
    if cp.exists():
        return json.loads(cp.read_text(encoding="utf-8"))
    return {"files": {}}


def save_cache(output_dir: Path, cache: dict) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    _cache_path(output_dir).write_text(
        json.dumps(cache, indent=2, ensure_ascii=False), encoding="utf-8"
    )


def should_skip(md_path: Path, cache: dict, output_dir: Path) -> bool:
    abs_str = str(md_path.resolve())
    entry = cache.get("files", {}).get(abs_str)
    if entry is None:
        return False
    try:
        if entry["mtime"] != md_path.stat().st_mtime:
            return False
    except OSError:
        return False
    for out_name in entry.get("outputs", []):
        if not (output_dir / out_name).is_file():
            return False
    return True


def update_cache(cache: dict, md_path: Path, output_bins: list[Path]) -> None:
    abs_str = str(md_path.resolve())
    cache.setdefault("files", {})[abs_str] = {
        "mtime": md_path.stat().st_mtime,
        "outputs": [p.name for p in output_bins],
    }


# ── Per-file conversion (section-aware) ─────────────────────────────────────

def render_snippet_to_bin(snippet_md: str, stem: str, output_dir: Path) -> list[Path]:
    """Render a plain markdown snippet to one or more (physically sliced) .bins."""
    html = render_markdown_to_html(snippet_md)
    html_path = output_dir / f"{stem}_t.html"
    png_path = output_dir / f"{stem}_t.png"

    try:
        html_path.write_text(html, encoding="utf-8")
        capture_full_page(html_path, png_path)
        img = normalize_image_to_l8(png_path)
        return slice_and_pack(img, stem, output_dir)
    finally:
        for p in (html_path, png_path):
            if p.exists():
                p.unlink()


def convert_markdown_to_bin(md_path: Path, output_dir: Path) -> tuple[list[Path], dict]:
    """Semantic conversion: returns (all_output_bins, tree_fragment).

    tree_fragment has the shape needed by generate_index:
        {filename: {h1_title: {h2_title: [{title: ..., file: ...}, ...], ...}}}
    """
    if not md_path.exists():
        raise FileNotFoundError(f"Markdown file not found: {md_path}")
    if md_path.suffix.lower() != ".md":
        raise ValueError(f"Input file must be .md: {md_path}")

    md_text = md_path.read_text(encoding="utf-8")
    md_text = resolve_images(md_text, md_path)

    sections_result = parse_markdown_sections(md_text)

    all_bins: list[Path] = []
    filename = md_path.stem
    tree: dict = {filename: OrderedDict()}

    for h1_title, h2_list in sections_result:
        h1_dict: OrderedDict = OrderedDict()
        for h2_title, h3_entries in h2_list:
            chapter_entries: list[dict] = []
            for h3_title, content in h3_entries:
                stem = content_stem(content)
                out_bins = render_snippet_to_bin(content, stem, output_dir)
                all_bins.extend(out_bins)

                display = h3_title if h3_title else h2_title
                for b in out_bins:
                    chapter_entries.append({"title": display, "file": b.name})

            if chapter_entries:
                h1_dict[h2_title] = chapter_entries

        if h1_dict:
            tree[filename][h1_title] = h1_dict

    return all_bins, tree


# ── OTA tree index ─────────────────────────────────────────────────────────

def generate_index(tree: dict, output_dir: Path) -> Path:
    """Write the hierarchical index.json from accumulated tree fragments."""
    index = {
        "toc": tree,
        "updated_at": int(time.time()),
    }
    idx_path = output_dir / INDEX_FILE
    idx_path.write_text(json.dumps(index, indent=2, ensure_ascii=False), encoding="utf-8")
    return idx_path


# ── Batch processing ───────────────────────────────────────────────────────

def process_directory(input_dir: Path, output_dir: Path | None = None) -> int:
    if output_dir is None:
        output_dir = input_dir / "_bin_output"
    output_dir = output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    input_dir = input_dir.resolve()

    md_files = sorted(input_dir.glob("*.md"))
    if not md_files:
        print("No .md files found.")
        return 0

    cache = load_cache(output_dir)
    full_tree: dict = {}
    converted = 0
    skipped = 0

    for md_path in md_files:
        if md_path.name.endswith("_temp.md"):
            continue
        if should_skip(md_path, cache, output_dir):
            print(f"  SKIP {md_path.name} (unchanged)")
            skipped += 1
            continue

        print(f"  CONV {md_path.name} ...", end=" ", flush=True)
        try:
            out_bins, tree_frag = convert_markdown_to_bin(md_path, output_dir)
        except Exception as exc:
            print(f"FAILED: {exc}")
            continue

        total_kb = sum(p.stat().st_size for p in out_bins) // 1024
        n_sections = sum(len(v) for v in tree_frag.get(list(tree_frag.keys())[0], {}).values()) if tree_frag else 0
        print(f"OK  ({total_kb} KB, {n_sections} sections)")
        update_cache(cache, md_path, out_bins)
        full_tree.update(tree_frag)
        converted += 1

    save_cache(output_dir, cache)
    if full_tree:
        idx = generate_index(full_tree, output_dir)
        print(f"\nDone: {converted} converted, {skipped} skipped.")
        print(f"Index: {idx}")
    else:
        print(f"\nDone: {converted} converted, {skipped} skipped. (no sections generated)")
    return converted


# ── CLI ────────────────────────────────────────────────────────────────────

def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Semantically slice .md files under a directory to LVGL v9 L8 .bin assets."
    )
    parser.add_argument("input_dir", help="Directory containing .md files to convert")
    parser.add_argument(
        "--output-dir",
        default=None,
        help="Output directory for .bins and cache (default: input_dir/_bin_output)",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    input_dir = Path(args.input_dir).resolve()
    if not input_dir.is_dir():
        print(f"Error: not a directory: {input_dir}", file=sys.stderr)
        return 1
    output_dir = Path(args.output_dir).resolve() if args.output_dir else None
    process_directory(input_dir, output_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
