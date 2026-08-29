#!/usr/bin/env python3
"""
Stage 2b: Ground-Truth Pixel Buffer Verification.
Extracts ground truth raw pixels from Pillow (RGB/RGBA),
and compares byte-for-byte against pngdecoder's --dump-pixels output.
Performs full-buffer diff, spot-checks corners/center, and validates alpha channel.
"""
import os
import sys
import glob
import tempfile
import subprocess
from PIL import Image

def verify_pixels(pngdecoder_bin, png_path, verbose=True):
    with Image.open(png_path) as im:
        mode = im.mode
        # We support RGB and RGBA
        if mode not in ('RGB', 'RGBA'):
            # Convert if necessary or handle
            if 'A' in mode:
                im = im.convert('RGBA')
                mode = 'RGBA'
            else:
                im = im.convert('RGB')
                mode = 'RGB'
        
        w, h = im.size
        bpp = 4 if mode == 'RGBA' else 3
        ground_truth = im.tobytes()
        px = im.load()

    with tempfile.NamedTemporaryFile(delete=False) as tmp:
        tmp_path = tmp.name

    try:
        cmd = [pngdecoder_bin, png_path, '--dump-pixels', tmp_path, '--info']
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if proc.returncode != 0:
            if verbose:
                print(f"  FAIL (decoder returned {proc.returncode}): {png_path}")
                print(f"       stderr: {proc.stderr.decode('utf-8', errors='replace').strip()}")
            return False

        with open(tmp_path, 'rb') as f:
            mine = f.read()

        expected_size = w * h * bpp
        if len(mine) != expected_size:
            if verbose:
                print(f"  FAIL (size mismatch): {png_path}")
                print(f"       expected {expected_size} bytes ({w}x{h}x{bpp}), got {len(mine)} bytes")
            return False

        # Spot checks: corners, center, intermediate
        spot_coords = [
            (0, 0),
            (w - 1, 0),
            (0, h - 1),
            (w - 1, h - 1),
            (w // 2, h // 2),
            (w // 4, h // 4),
            (3 * w // 4, 3 * h // 4)
        ]
        # Filter coords to unique within bounds
        spot_coords = list(dict.fromkeys([(min(x, w - 1), min(y, h - 1)) for x, y in spot_coords]))

        for (sx, sy) in spot_coords:
            offset = (sy * w + sx) * bpp
            my_pixel = tuple(mine[offset : offset + bpp])
            pil_pixel = px[sx, sy]
            if isinstance(pil_pixel, int):
                pil_pixel = (pil_pixel,)
            if my_pixel != pil_pixel:
                if verbose:
                    print(f"  FAIL (spot-check mismatch at ({sx}, {sy})): {png_path}")
                    print(f"       expected {pil_pixel}, got {my_pixel}")
                return False

        # Alpha channel check for RGBA
        if mode == 'RGBA':
            mine_alpha = mine[3::4]
            truth_alpha = ground_truth[3::4]
            if mine_alpha != truth_alpha:
                if verbose:
                    print(f"  FAIL (alpha channel mismatch): {png_path}")
                    for idx, (ma, ta) in enumerate(zip(mine_alpha, truth_alpha)):
                        if ma != ta:
                            ay = idx // w
                            ax = idx % w
                            print(f"       first alpha mismatch at pixel ({ax}, {ay}): expected {ta}, got {ma}")
                            break
                return False

        # Full-buffer comparison
        if mine == ground_truth:
            if verbose:
                alpha_note = " + alpha verified" if mode == "RGBA" else ""
                print(f"  PASS: {png_path:<40} ({len(ground_truth):>8} bytes, {w}x{h} {mode}{alpha_note}, exact match)")
            return True
        else:
            if verbose:
                print(f"  FAIL (full-buffer mismatch): {png_path}")
                for i in range(len(ground_truth)):
                    if ground_truth[i] != mine[i]:
                        py = i // (w * bpp)
                        px_idx = (i % (w * bpp)) // bpp
                        chan = (i % (w * bpp)) % bpp
                        chan_name = ['R', 'G', 'B', 'A'][chan]
                        print(f"       first diff at byte {i} (pixel ({px_idx}, {py}) {chan_name}): expected {ground_truth[i]}, got {mine[i]}")
                        break
            return False

    finally:
        if os.path.exists(tmp_path):
            os.remove(tmp_path)

def main():
    bin_name = 'build/pngdecoder.exe' if os.name == 'nt' else 'build/pngdecoder'
    if len(sys.argv) > 1:
        bin_name = sys.argv[1]

    if not os.path.exists(bin_name):
        print(f"Error: binary '{bin_name}' not found. Run make first.")
        sys.exit(1)

    files = sorted(glob.glob('demo/*.png') + glob.glob('tests/fixtures/*.png'))
    if not files:
        print("No PNG files found in demo/ or tests/fixtures/")
        sys.exit(1)

    print(f"=== Running Stage 2b: Ground-Truth Pixel Buffer Verification ({len(files)} files) ===")
    all_ok = True
    for f in files:
        ok = verify_pixels(bin_name, f, verbose=True)
        if not ok:
            all_ok = False

    print("\n" + ("=" * 60))
    if all_ok:
        print(f"ALL {len(files)} FILES PASSED: Pixel buffer is 100% byte-for-byte identical to Pillow ground truth.")
        sys.exit(0)
    else:
        print("SOME FILES FAILED PIXEL VERIFICATION.")
        sys.exit(1)

if __name__ == '__main__':
    main()
