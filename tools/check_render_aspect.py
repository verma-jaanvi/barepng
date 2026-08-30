"""
Phase 7 regression check: term_render.c used to hardcode row_scale=2,
so downscaling --width without also downscaling rows produced a badly
distorted (vertically stretched) render for any width < image width.
Fixed by scaling rows at the same ratio as columns (see term_render.c).

This isn't a unit test of term_render's internals (it has no return
value to assert on directly, only stdout side effects) - it's a
black-box check against the built binary, run the same way the demo
itself will be run, so it catches the exact failure mode a judge would
see live.

Usage: python3 tools/check_render_aspect.py [path-to-binary]
Exit 0 on pass, 1 on any mismatch.
"""
import subprocess
import sys
import os

BIN = sys.argv[1] if len(sys.argv) > 1 else os.path.join("build", "pngdecoder")

# (demo file, native width, native height, requested --width, expected output rows)
# expected rows = ceil(requested_width * height / width / 2)
CASES = [
    ("demo/photo_640x480_rgb.png", 640, 480, 90, 34),
    ("demo/icon_32x32_rgb.png", 32, 32, 32, 16),
    ("demo/alpha_256x256_rgba.png", 256, 256, 64, 32),
]

def count_image_rows(raw: bytes) -> int:
    lines = raw.split(b"\n")
    return sum(1 for l in lines if l.startswith(b"\x1b["))

def main() -> int:
    failed = 0
    for path, w, h, req_width, expected in CASES:
        out = subprocess.run(
            [BIN, path, "--mode=truecolor", "--width", str(req_width)],
            capture_output=True,
        )
        rows = count_image_rows(out.stdout)
        status = "PASS" if rows == expected else "FAIL"
        if rows != expected:
            failed += 1
        print(f"{status}: {path} --width {req_width} -> {rows} rows (expected {expected})")

    if failed:
        print(f"\n{failed} case(s) failed - row scaling does not match column scaling.")
        return 1
    print(f"\nAll {len(CASES)} aspect-ratio cases passed.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
