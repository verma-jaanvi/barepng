from PIL import Image
import sys

src = sys.argv[1]
dst = sys.argv[2]

img = Image.open(src).convert("RGB")
img.thumbnail((640, 640))
img.save(dst)
print(f"saved {dst}  size={img.size}")