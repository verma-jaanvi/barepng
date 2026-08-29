#!/usr/bin/env python3
"""
Stage 2c: Rendered Output (Phase 4) Verification.
1. Decodes ANSI truecolor sequences emitted by pngdecoder back into an RGB pixel grid.
2. Compares reconstructed grid against reference downsampled & composited pixels.
3. Tests full resolution and downscaling (--width).
4. Verifies alpha compositing on transparent, opaque, and gradient RGBA pixels.
"""
import os
import re
import sys
import glob
import subprocess
from PIL import Image

def composite_channel(src, alpha, bg):
    return (alpha * src + (255 - alpha) * bg + 127) // 255

def sample_reference_grid(png_path, out_cols, bg=(128, 128, 128)):
    with Image.open(png_path) as im:
        mode = im.mode
        if mode not in ('RGB', 'RGBA'):
            im = im.convert('RGBA' if 'A' in mode else 'RGB')
            mode = im.mode
        w, h = im.size
        px = im.load()

    out_cols = min(w, out_cols)
    scale_x = (w << 16) // out_cols
    row_scale = 2 * scale_x
    out_rows = ((h << 16) + row_scale - 1) // row_scale
    if out_rows == 0:
        out_rows = 1

    expected_top = []
    expected_bot = []

    for yt in range(out_rows):
        img_row_top = (yt * row_scale + (row_scale >> 2)) >> 16
        img_row_bot = (yt * row_scale + (row_scale * 3 >> 2)) >> 16
        if img_row_top >= h: img_row_top = h - 1
        if img_row_bot >= h: img_row_bot = h - 1

        top_line = []
        bot_line = []

        for xt in range(out_cols):
            img_col = (xt * scale_x + (scale_x >> 1)) >> 16
            if img_col >= w: img_col = w - 1

            p_top = px[img_col, img_row_top]
            p_bot = px[img_col, img_row_bot]

            if mode == 'RGBA':
                tr = composite_channel(p_top[0], p_top[3], bg[0])
                tg = composite_channel(p_top[1], p_top[3], bg[1])
                tb = composite_channel(p_top[2], p_top[3], bg[2])

                br = composite_channel(p_bot[0], p_bot[3], bg[0])
                bg_p = composite_channel(p_bot[1], p_bot[3], bg[1])
                bb = composite_channel(p_bot[2], p_bot[3], bg[2])
            else:
                tr, tg, tb = p_top[0], p_top[1], p_top[2]
                br, bg_p, bb = p_bot[0], p_bot[1], p_bot[2]

            top_line.append((tr, tg, tb))
            bot_line.append((br, bg_p, bb))

        expected_top.append(top_line)
        expected_bot.append(bot_line)

    return out_cols, out_rows, expected_top, expected_bot

# Regex matching: \x1b[38;2;R;G;Bm\x1b[48;2;R;G;Bm▀
ANSI_BLOCK_RE = re.compile(
    r'\x1b\[38;2;(\d+);(\d+);(\d+)m\x1b\[48;2;(\d+);(\d+);(\d+)m\u2580'
)

def parse_rendered_ansi(stdout_text):
    # Split header info lines and render body
    lines = stdout_text.splitlines()
    # Find blank line separating header and render output
    render_lines = []
    found_blank = False
    for line in lines:
        if not found_blank:
            if line.strip() == '':
                found_blank = True
            continue
        if line.strip():
            render_lines.append(line)

    parsed_top = []
    parsed_bot = []

    for line in render_lines:
        # Strip trailing \x1b[0m
        if line.endswith('\x1b[0m'):
            line = line[:-4]
        matches = ANSI_BLOCK_RE.findall(line)
        if not matches:
            continue
        top_row = []
        bot_row = []
        for m in matches:
            tr, tg, tb, br, bg_p, bb = map(int, m)
            top_row.append((tr, tg, tb))
            bot_row.append((br, bg_p, bb))
        parsed_top.append(top_row)
        parsed_bot.append(bot_row)

    return parsed_top, parsed_bot

def verify_render_truecolor(bin_path, png_path, max_cols=80):
    cmd = [bin_path, png_path, '--mode=truecolor', f'--width={max_cols}']
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        print(f"  FAIL: {png_path} returned {proc.returncode}")
        print(proc.stderr.decode('utf-8', errors='replace'))
        return False

    raw_stdout = proc.stdout.decode('utf-8', errors='replace')
    parsed_top, parsed_bot = parse_rendered_ansi(raw_stdout)

    out_cols, out_rows, exp_top, exp_bot = sample_reference_grid(png_path, max_cols)

    if len(parsed_top) != out_rows:
        print(f"  FAIL: {png_path} row count mismatch: expected {out_rows}, got {len(parsed_top)}")
        return False

    for y in range(out_rows):
        if len(parsed_top[y]) != out_cols:
            print(f"  FAIL: {png_path} col count mismatch at row {y}: expected {out_cols}, got {len(parsed_top[y])}")
            return False
        for x in range(out_cols):
            if parsed_top[y][x] != exp_top[y][x]:
                print(f"  FAIL: {png_path} top pixel mismatch at ({x}, {y}): expected {exp_top[y][x]}, got {parsed_top[y][x]}")
                return False
            if parsed_bot[y][x] != exp_bot[y][x]:
                print(f"  FAIL: {png_path} bot pixel mismatch at ({x}, {y}): expected {exp_bot[y][x]}, got {parsed_bot[y][x]}")
                return False

    return True

def verify_alpha_compositing_rules(bin_path):
    """
    Directly verify transparent, opaque, and semi-transparent alpha rules.
    """
    rgba_files = ['demo/alpha_256x256_rgba.png', 'tests/fixtures/gradient_rgba.png', 'demo/large_alpha_2000x1500_rgba.png']
    bg = (128, 128, 128)
    all_ok = True

    for alpha_file in rgba_files:
        if not os.path.exists(alpha_file):
            continue

        # Use native width or capped width
        cmd = [bin_path, alpha_file, '--mode=truecolor', '--width', '128']
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if proc.returncode != 0:
            return False

        parsed_top, parsed_bot = parse_rendered_ansi(proc.stdout.decode('utf-8', errors='replace'))
        
        with Image.open(alpha_file) as im:
            px = im.load()
            w, h = im.size

        checked_transparent = 0
        checked_opaque = 0
        checked_blend = 0

        out_cols = min(w, 128)
        scale_x = (w << 16) // out_cols
        row_scale = 2 * scale_x
        out_rows = ((h << 16) + row_scale - 1) // row_scale

        for y in range(min(len(parsed_top), out_rows)):
            img_row_top = (y * row_scale + (row_scale >> 2)) >> 16
            img_row_bot = (y * row_scale + (row_scale * 3 >> 2)) >> 16
            if img_row_top >= h: img_row_top = h - 1
            if img_row_bot >= h: img_row_bot = h - 1

            for x in range(min(len(parsed_top[y]), out_cols)):
                img_col = (x * scale_x + (scale_x >> 1)) >> 16
                if img_col >= w: img_col = w - 1

                for row_idx, r_px, sample_y in [(0, parsed_top[y][x], img_row_top), (1, parsed_bot[y][x], img_row_bot)]:
                    src_r, src_g, src_b, src_a = px[img_col, sample_y]
                    if src_a == 0:
                        if r_px != bg:
                            print(f"  FAIL: alpha=0 did not composite to bg (128,128,128), got {r_px}")
                            all_ok = False
                        checked_transparent += 1
                    elif src_a == 255:
                        if r_px != (src_r, src_g, src_b):
                            print(f"  FAIL: alpha=255 did not preserve source ({src_r},{src_g},{src_b}), got {r_px}")
                            all_ok = False
                        checked_opaque += 1
                    else:
                        exp = (
                            composite_channel(src_r, src_a, bg[0]),
                            composite_channel(src_g, src_a, bg[1]),
                            composite_channel(src_b, src_a, bg[2])
                        )
                        if r_px != exp:
                            print(f"  FAIL: alpha={src_a} blend mismatch: expected {exp}, got {r_px}")
                            all_ok = False
                        checked_blend += 1

        print(f"  PASS: Alpha compositing verified on {alpha_file:<35} ({checked_transparent:>5} transparent, {checked_opaque:>5} opaque, {checked_blend:>6} blended pixels)")
    return all_ok

def main():
    bin_name = 'build/pngdecoder.exe' if os.name == 'nt' else 'build/pngdecoder'
    if len(sys.argv) > 1:
        bin_name = sys.argv[1]

    if not os.path.exists(bin_name):
        print(f"Error: binary '{bin_name}' not found. Run make first.")
        sys.exit(1)

    files = sorted(glob.glob('demo/*.png') + glob.glob('tests/fixtures/*.png'))
    print(f"=== Running Stage 2c: Rendered Output & ANSI Reconstruction Verification ({len(files)} files) ===")

    all_ok = True

    # Test 1: Full-resolution ANSI decode and grid match (where applicable)
    print("\n--- 1. Full-Resolution / Natural Width ANSI Grid Check ---")
    for f in files:
        ok = verify_render_truecolor(bin_name, f, max_cols=256)
        if ok:
            print(f"  PASS: {f:<40} (ANSI parsed -> RGB grid exact match)")
        else:
            all_ok = False

    # Test 2: Downsampled ANSI decode and aspect-ratio scale check (width=40 & width=80)
    print("\n--- 2. Downscaled ANSI Grid Check (width=40 & width=80) ---")
    for f in ['demo/photo_640x480_rgb.png', 'demo/large_2048x2048_rgb.png', 'demo/icon_32x32_rgb.png']:
        if os.path.exists(f):
            for w in [40, 80]:
                ok = verify_render_truecolor(bin_name, f, max_cols=w)
                if ok:
                    print(f"  PASS: {f:<35} width={w:<3} (Downsampled ANSI grid exact match)")
                else:
                    all_ok = False

    # Test 3: Dedicated Alpha Compositing Verification
    print("\n--- 3. Dedicated Alpha Compositing Rules Check ---")
    if not verify_alpha_compositing_rules(bin_name):
        all_ok = False

    # Test 4: ASCII and 256-color modes sanity check
    print("\n--- 4. Multi-Mode Formatting Check (ascii & 256color) ---")
    for mode in ['ascii', '256']:
        cmd = [bin_name, 'demo/icon_32x32_rgb.png', f'--mode={mode}']
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if proc.returncode == 0:
            print(f"  PASS: mode={mode:<8} rendered cleanly (exit code 0)")
        else:
            print(f"  FAIL: mode={mode} returned error {proc.returncode}")
            all_ok = False

    print("\n" + ("=" * 60))
    if all_ok:
        print("ALL RENDER TESTS PASSED: Terminal ANSI output, downscaling, half-blocks, and alpha compositing verified 100%.")
        sys.exit(0)
    else:
        print("SOME RENDER TESTS FAILED.")
        sys.exit(1)

if __name__ == '__main__':
    main()
