#!/usr/bin/env python3
"""
Generate the demo/ test PNG set.

All images are forced to 8-bit depth, non-interlaced, and either
truecolor (RGB) or truecolor+alpha (RGBA) — matching the locked scope
in SCOPE.md. Pillow's default PNG writer already does 8-bit/no-interlace
for these modes, but we double check after writing.
"""
import os
import random
import struct
from PIL import Image

OUT = os.path.join(os.path.dirname(__file__), "..", "demo")
os.makedirs(OUT, exist_ok=True)

random.seed(42)


def save_png(img, name):
    path = os.path.join(OUT, name)
    # PNG_SAVE with no interlace, no palette optimization
    img.save(path, format="PNG", optimize=False, interlace=False)
    return path


def check_ihdr(path):
    """Sanity check bit depth / color type / interlace match scope lock."""
    with open(path, "rb") as f:
        data = f.read(33)
    assert data[:8] == b"\x89PNG\r\n\x1a\n", f"{path}: bad signature"
    # bytes 16-20 of file = IHDR data start (8 sig + 4 len + 4 'IHDR')
    width, height, bitdepth, colortype, comp, filt, interlace = struct.unpack(
        ">IIBBBBB", data[16:29]
    )
    assert bitdepth == 8, f"{path}: bit depth {bitdepth} != 8"
    assert colortype in (2, 6), f"{path}: color type {colortype} not RGB/RGBA truecolor"
    assert interlace == 0, f"{path}: interlaced, out of scope"
    ctname = "RGB" if colortype == 2 else "RGBA"
    print(f"  {os.path.basename(path):20s} {width}x{height:<6d} 8-bit {ctname:5s} interlace=0  OK")


# 1) small icon, RGB
icon = Image.new("RGB", (32, 32))
px = icon.load()
for y in range(32):
    for x in range(32):
        px[x, y] = (x * 8 % 256, y * 8 % 256, (x + y) * 4 % 256)
save_png(icon, "icon_32x32_rgb.png")

# 2) "photo" style, RGB — smooth gradient + noise to mimic photographic entropy
photo = Image.new("RGB", (640, 480))
px = photo.load()
for y in range(480):
    for x in range(640):
        r = int(255 * x / 640)
        g = int(255 * y / 480)
        b = (r + g) // 2
        n = random.randint(-12, 12)
        px[x, y] = (
            max(0, min(255, r + n)),
            max(0, min(255, g + n)),
            max(0, min(255, b + n)),
        )
save_png(photo, "photo_640x480_rgb.png")

# 3) alpha channel, RGBA — soft circular gradient fading to transparent
alpha = Image.new("RGBA", (256, 256))
px = alpha.load()
cx, cy = 128, 128
for y in range(256):
    for x in range(256):
        d = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
        a = max(0, min(255, int(255 - d)))
        px[x, y] = (200, 60, 90, a)
save_png(alpha, "alpha_256x256_rgba.png")

# 4) large, RGB, 2000px+ for perf testing
large = Image.new("RGB", (2048, 2048))
px = large.load()
for y in range(2048):
    for x in range(2048):
        px[x, y] = ((x ^ y) % 256, (x * 3) % 256, (y * 5) % 256)
save_png(large, "large_2048x2048_rgb.png")

# 5) large + alpha, RGBA, to stress-test IDAT chunking on a big alpha image
large_a = Image.new("RGBA", (2000, 1500))
px = large_a.load()
for y in range(1500):
    for x in range(2000):
        px[x, y] = ((x * 7) % 256, (y * 7) % 256, ((x + y) * 3) % 256, (x % 256))
save_png(large_a, "large_alpha_2000x1500_rgba.png")

print("\nSanity-checking IHDR fields against scope lock:\n")
for f in sorted(os.listdir(OUT)):
    if f.endswith(".png"):
        check_ihdr(os.path.join(OUT, f))

print("\nAll demo PNGs generated and verified in scope.")
