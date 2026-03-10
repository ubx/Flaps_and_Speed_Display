from PIL import Image, ImageDraw
import argparse

parser = argparse.ArgumentParser(description="Convert image to 466x466 circular PNG")
parser.add_argument("input", help="Input image file")
parser.add_argument("-o", "--output", help="Output file name (default: round_466.png)", default="round_466.png")

args = parser.parse_args()

size = 466

img = Image.open(args.input).convert("RGBA")

w, h = img.size
side = min(w, h)

img = img.crop((
    (w - side) // 2,
    (h - side) // 2,
    (w + side) // 2,
    (h + side) // 2
))

img = img.resize((size, size), Image.LANCZOS)

mask = Image.new("L", (size, size), 0)
ImageDraw.Draw(mask).ellipse((0, 0, size - 1, size - 1), fill=255)

out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
out.paste(img, (0, 0), mask)

out.save(args.output)

print(f"Saved {args.output}")