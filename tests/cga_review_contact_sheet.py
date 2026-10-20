#!/usr/bin/env python3
"""Build a labeled 2x2 contact sheet for the four-view complex review."""

import argparse
import glob
import os
import sys

from PIL import Image, ImageDraw, ImageFont

DEFAULT_LABELS = (
    "Forecourt / paifang",
    "Main hall / two-storey",
    "South courtyard / gate",
    "Upper roof close / beasts",
)


def load_font(size):
    for path in (
        "/usr/share/fonts/gsfonts/NimbusSans-Regular.otf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    ):
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("screenshots", help="directory containing four review PNGs")
    parser.add_argument("-o", "--output", default="cga_review.png")
    parser.add_argument("--labels", nargs=4, default=list(DEFAULT_LABELS))
    args = parser.parse_args()

    files = sorted(glob.glob(os.path.join(args.screenshots, "*.png")))
    if len(files) != 4:
        print(f"expected 4 review screenshots, found {len(files)}", file=sys.stderr)
        return 2

    width, height = 640, 360
    sheet = Image.new("RGB", (width * 2, (height + 30) * 2), (28, 28, 30))
    draw = ImageDraw.Draw(sheet)
    font = load_font(18)
    for index, (path, label) in enumerate(zip(files, args.labels)):
        image = Image.open(path).convert("RGB").resize((width, height), Image.LANCZOS)
        x = (index % 2) * width
        y = (index // 2) * (height + 30)
        sheet.paste(image, (x, y + 30))
        draw.rectangle([x, y, x + width, y + 30], fill=(45, 45, 48))
        draw.text((x + 8, y + 6), label, fill=(235, 235, 235), font=font)
    sheet.save(args.output)
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
