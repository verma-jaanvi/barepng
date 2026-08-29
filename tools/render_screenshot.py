#!/usr/bin/env python3
"""Render imgview's real ANSI truecolor output into a terminal-window PNG.

This is not a mockup: it parses the actual escape sequences imgview emits
(38;2;r;g;b foreground, 48;2;r;g;b background, U+2580 half-block) and paints
each cell as a real pixel block, then wraps it in a plain terminal chrome
(title dots + a prompt line) so the README screenshot reflects the true
program output rather than a hand-drawn approximation.
"""
import re
import sys
from PIL import Image, ImageDraw, ImageFont

RAW_PATH = sys.argv[1]
OUT_PATH = sys.argv[2]
PROMPT_LINE = sys.argv[3] if len(sys.argv) > 3 else "$ imgview demo/photo_640x480_rgb.png --width 100"

CELL_W = 8
CELL_H = 16

ANSI_RE = re.compile(r'\x1b\[([0-9;]*)m')

def parse_lines(text):
    """Return list of rows; each row is list of (fg, bg, char)."""
    rows = []
    for raw_line in text.split('\n'):
        fg = (255, 255, 255)
        bg = None
        cells = []
        i = 0
        n = len(raw_line)
        while i < n:
            m = ANSI_RE.match(raw_line, i)
            if m:
                codes = m.group(1).split(';') if m.group(1) else ['0']
                j = 0
                while j < len(codes):
                    c = codes[j]
                    if c == '0':
                        fg = (255, 255, 255)
                        bg = None
                    elif c == '38' and j + 4 < len(codes) and codes[j+1] == '2':
                        fg = (int(codes[j+2]), int(codes[j+3]), int(codes[j+4]))
                        j += 4
                    elif c == '48' and j + 4 < len(codes) and codes[j+1] == '2':
                        bg = (int(codes[j+2]), int(codes[j+3]), int(codes[j+4]))
                        j += 4
                    j += 1
                i = m.end()
                continue
            ch = raw_line[i]
            cells.append((fg, bg, ch))
            i += 1
        rows.append(cells)
    return rows

def render(rows, prompt_line, out_path):
    max_cols = max((len(r) for r in rows), default=1)
    term_w = max(max_cols * CELL_W, 620)
    body_h = len(rows) * CELL_H
    top_bar = 34
    prompt_h = 26
    pad = 14
    img_w = term_w + pad * 2
    img_h = top_bar + prompt_h + body_h + pad * 2

    img = Image.new('RGB', (img_w, img_h), (18, 18, 20))
    draw = ImageDraw.Draw(img)

    # title bar
    draw.rectangle([0, 0, img_w, top_bar], fill=(45, 45, 48))
    for k, col in enumerate([(255, 95, 86), (255, 189, 46), (39, 201, 63)]):
        cx = 18 + k * 22
        draw.ellipse([cx, top_bar // 2 - 6, cx + 12, top_bar // 2 + 6], fill=col)

    try:
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 13)
        title_font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 13)
    except Exception:
        font = ImageFont.load_default()
        title_font = font

    draw.text((img_w // 2 - 30, top_bar // 2 - 8), "imgview", fill=(210, 210, 210), font=title_font)

    y = top_bar + pad // 2
    draw.text((pad, y), prompt_line, fill=(120, 220, 120), font=font)
    y += prompt_h

    for row in rows:
        x = pad
        for fg, bg, ch in row:
            cell_bg = bg if bg is not None else (18, 18, 20)
            draw.rectangle([x, y, x + CELL_W, y + CELL_H], fill=cell_bg)
            if ch == '\u2580':
                # upper half block: top half fg, bottom half bg (already painted)
                draw.rectangle([x, y, x + CELL_W, y + CELL_H // 2], fill=fg)
            elif ch not in (' ', ''):
                draw.text((x, y - 2), ch, fill=fg, font=font)
            x += CELL_W
        y += CELL_H

    img.save(out_path)
    print(f"wrote {out_path} ({img_w}x{img_h})")

if __name__ == '__main__':
    with open(RAW_PATH, 'r', encoding='utf-8', errors='replace') as f:
        text = f.read()
    rows = parse_lines(text)
    render(rows, PROMPT_LINE, OUT_PATH)
