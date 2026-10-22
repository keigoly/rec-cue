#!/usr/bin/env python3
"""Rec Cue! のアイコン: 案内役の絵 (正方形の PNG) を角丸の四角に収めて、複数の大きさの .ico に書き出す。

16〜64 px は 32 bit の DIB、96 px 以上は PNG で格納する (どちらも Windows Vista 以降で読める)。
原画は res/icon-src.png (NovelAI V5 の S-L1P1・seed 26102201 を 512 px にしたもの)。

usage: uv run --with pillow python3 tools/make_icon.py res/icon-src.png res/app.ico [--preview <png>]
"""
import argparse
import io
import struct

from PIL import Image, ImageDraw

SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
RADIUS = 0.2   # corner radius as a share of the side


def rounded(src: Image.Image, n: int) -> Image.Image:
    """src を n x n の角丸の四角にする。小さいサイズは余白なし (顔を大きく)。"""
    margin = 0 if n <= 32 else round(n * 0.03)
    side = n - 2 * margin
    art = src.convert("RGBa").resize((side, side), Image.LANCZOS).convert("RGBA")
    ss = 4   # supersampled mask for a smooth edge
    m = Image.new("L", (side * ss, side * ss), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, side * ss - 1, side * ss - 1), radius=side * ss * RADIUS, fill=255)
    m = m.resize((side, side), Image.LANCZOS)
    a = Image.composite(art.getchannel("A"), Image.new("L", (side, side), 0), m)
    art.putalpha(a)
    out = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    out.paste(art, (margin, margin))
    return out


def dib(img: Image.Image) -> bytes:
    n = img.width
    header = struct.pack("<IiiHHIIiiII", 40, n, n * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    pix = bytearray()
    for y in range(n - 1, -1, -1):   # bottom-up
        for x in range(n):
            r, g, b, a = img.getpixel((x, y))
            pix += bytes((b, g, r, a))
    mask = bytes(((n + 31) // 32) * 4 * n)   # transparency comes from the alpha channel
    return header + bytes(pix) + mask


def png(img: Image.Image) -> bytes:
    b = io.BytesIO()
    img.save(b, "PNG", optimize=True)
    return b.getvalue()


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("ico")
    ap.add_argument("--preview", help="write every size side by side to this PNG (for checking)")
    a = ap.parse_args()
    src = Image.open(a.src)
    imgs = [rounded(src, n) for n in SIZES]
    blobs = [dib(i) if i.width <= 64 else png(i) for i in imgs]
    head = struct.pack("<HHH", 0, 1, len(SIZES))
    off = 6 + 16 * len(SIZES)
    entries = b""
    for n, blob in zip(SIZES, blobs):
        entries += struct.pack("<BBBBHHII", n % 256, n % 256, 0, 0, 1, 32, len(blob), off)
        off += len(blob)
    with open(a.ico, "wb") as f:
        f.write(head + entries + b"".join(blobs))
    if a.preview:
        w = sum(i.width for i in imgs) + 8 * len(imgs)
        sheet = Image.new("RGBA", (w, 256), (32, 36, 44, 255))
        x = 0
        for i in imgs:
            sheet.alpha_composite(i, (x, 256 - i.height))
            x += i.width + 8
        sheet.save(a.preview)
    print(f"{a.ico}: {len(SIZES)} sizes, {off} bytes")


if __name__ == "__main__":
    main()
