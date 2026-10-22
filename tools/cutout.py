#!/usr/bin/env python3
"""白い地の絵 (NovelAI の SD キャラ) から地を抜いて透過 PNG にする。

縁から続く白だけを地とみなし (服の白は輪郭線で囲まれているので残る)、地の周り 3 px の
アンチエイリアスは白を差し引いて半透明にする (暗い画面で白い縁取りが出ないように)。

usage: uv run --with pillow --with numpy python3 tools/cutout.py <in.png> <out.png> [--height 256]
"""
import argparse

import numpy as np
from PIL import Image


def border_connected(cand: np.ndarray) -> np.ndarray:
    """cand (bool) のうち画像の縁につながっている部分。4 近傍で広げていく。"""
    m = np.zeros_like(cand)
    m[0, :], m[-1, :], m[:, 0], m[:, -1] = cand[0, :], cand[-1, :], cand[:, 0], cand[:, -1]
    while True:
        g = m.copy()
        g[1:, :] |= m[:-1, :]
        g[:-1, :] |= m[1:, :]
        g[:, 1:] |= m[:, :-1]
        g[:, :-1] |= m[:, 1:]
        g &= cand
        if (g == m).all():
            return m
        m = g


def dilate(m: np.ndarray, n: int) -> np.ndarray:
    for _ in range(n):
        g = m.copy()
        g[1:, :] |= m[:-1, :]
        g[:-1, :] |= m[1:, :]
        g[:, 1:] |= m[:, :-1]
        g[:, :-1] |= m[:, 1:]
        m = g
    return m


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--height", type=int, default=256)
    a = ap.parse_args()
    rgb = np.asarray(Image.open(a.src).convert("RGB")).astype(np.float32)
    dev = 255.0 - rgb.min(axis=2)                   # how far from white
    bg = border_connected(dev < 14)
    band = dilate(bg, 3) & ~bg
    alpha = np.ones(dev.shape, np.float32)
    alpha[bg] = 0
    # colour-to-alpha against white inside the band: c = a*c' + (1-a)*255
    ab = np.clip((255.0 - rgb[band].min(axis=1)) / 255.0 * 1.15, 0, 1)
    alpha[band] = ab
    out = rgb.copy()
    safe = np.maximum(ab, 1e-3)[:, None]
    out[band] = np.clip(255.0 - (255.0 - rgb[band]) / safe, 0, 255)
    rgba = np.dstack([out, alpha * 255.0]).round().astype(np.uint8)
    img = Image.fromarray(rgba, "RGBA")
    box = img.getchannel("A").point(lambda v: 255 if v > 8 else 0).getbbox()
    pad = 6
    img = img.crop((max(box[0] - pad, 0), max(box[1] - pad, 0), min(box[2] + pad, img.width), min(box[3] + pad, img.height)))
    w = round(img.width * a.height / img.height)
    img = img.convert("RGBa").resize((w, a.height), Image.LANCZOS).convert("RGBA")   # premultiplied: no dark fringe
    img.save(a.dst, optimize=True)
    print(f"{a.dst} {img.width}x{img.height} (bg {bg.mean() * 100:.0f}%)")


if __name__ == "__main__":
    main()
