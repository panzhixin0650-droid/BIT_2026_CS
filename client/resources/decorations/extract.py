"""Reproduce cutouts from the user-supplied sheet (Pillow only).

Remove only edge-connected neutral white, retain enclosed white, keep the
largest drawing, and unmatte the narrow exterior edge against white.
The input is read-only. Usage: python3 extract.py SOURCE.png OUTPUT_DIRECTORY
"""
import argparse
from collections import deque
from pathlib import Path

from PIL import Image, ImageFilter, ImageDraw

BOXES = {
    "clover": (70, 35, 328, 334),
    "avocado": (675, 80, 900, 327),
    "bow": (40, 397, 327, 654),
    "leaves": (397, 713, 606, 982),
    "rabbit": (1025, 742, 1271, 977),
    "drink": (80, 995, 285, 1291),
    "frog": (691, 1339, 949, 1584),
    "plant": (1002, 1337, 1253, 1586),
}


def neighbors(x, y, w, h):
    for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
        if 0 <= nx < w and 0 <= ny < h:
            yield nx, ny


def cutout(source, box):
    crop = source.crop(box).convert("RGB")
    w, h = crop.size
    pixels = crop.load()
    exterior = set()
    queue = deque([(x, y) for x in range(w) for y in (0, h - 1)]
                  + [(x, y) for y in range(h) for x in (0, w - 1)])
    while queue:
        x, y = queue.popleft()
        if (x, y) in exterior:
            continue
        rgb = pixels[x, y]
        if min(rgb) < 240 or max(rgb) - min(rgb) > 20:
            continue
        exterior.add((x, y))
        queue.extend(p for p in neighbors(x, y, w, h) if p not in exterior)

    remaining = {(x, y) for y in range(h) for x in range(w)} - exterior
    largest = set()
    while remaining:
        seed = remaining.pop()
        component, queue = {seed}, deque([seed])
        while queue:
            x, y = queue.popleft()
            for p in neighbors(x, y, w, h):
                if p in remaining:
                    remaining.remove(p)
                    component.add(p)
                    queue.append(p)
        if len(component) > len(largest):
            largest = component

    mask = Image.new("L", crop.size)
    for p in largest:
        mask.putpixel(p, 255)
    core = mask.filter(ImageFilter.MinFilter(5))
    result = Image.new("RGBA", crop.size)
    for x, y in largest:
        rgb = pixels[x, y]
        alpha = 1.0
        if not core.getpixel((x, y)):
            candidates = [(dx * dx + dy * dy, pixels[x + dx, y + dy])
                          for dy in range(-3, 4) for dx in range(-3, 4)
                          if 0 <= x + dx < w and 0 <= y + dy < h
                          and core.getpixel((x + dx, y + dy))]
            if candidates:
                reference = min(candidates, key=lambda item: item[0])[1]
                # Estimate white-background coverage using the strongest channel.
                channel = min(range(3), key=lambda i: reference[i])
                if reference[channel] < 230:
                    alpha = min(1.0, (255 - rgb[channel]) / (255 - reference[channel]))
                    alpha = max(alpha, .01)
                    rgb = tuple(round(max(0, min(255, 255 - (255 - v) / alpha))) for v in rgb)
        result.putpixel((x, y), (*rgb, round(alpha * 255)))
    result = result.crop(mask.getbbox())
    padded = Image.new("RGBA", (result.width + 16, result.height + 16))
    padded.paste(result, (8, 8))
    return padded


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    source = Image.open(args.source)
    if source.size != (1320, 1632):
        raise ValueError("Expected the original 1320 x 1632 sheet; do not rescale it")
    args.output.mkdir(parents=True, exist_ok=True)
    preview = Image.new("RGB", (1120, 600), "#f6f7f2")
    draw = ImageDraw.Draw(preview)
    for i, (name, box) in enumerate(BOXES.items()):
        asset = cutout(source, box)
        asset.save(args.output / (name + ".png"))
        thumb = asset.copy()
        thumb.thumbnail((190, 210), Image.LANCZOS)
        x, y = (i % 4) * 280, (i // 4) * 300
        draw.rectangle((x + 8, y + 8, x + 271, y + 260), fill="#244d40")
        preview.paste(thumb, (x + (280 - thumb.width) // 2, y + 28), thumb)
        draw.text((x + 20, y + 275), name, fill="#244d40")
        print(name, asset.size, "RGBA")
    preview.save(args.output / "cutouts-preview.png")


if __name__ == "__main__":
    main()
