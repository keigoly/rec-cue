#!/usr/bin/env python3
"""README の画像を作る (Mac・Pillow)。

  rec-cue.exe --ui-snapshot <dir>\\pub.png --dpi 192 --at 2026-10-22T14:00   (Windows で 2 倍の見本の画面を描く)
  uv run --with pillow python3 tools/make_readme_images.py <pub-*.png のあるフォルダー> docs/images

見本の画面は音量・機器名・フォルダーが作りもの (--ui-snapshot) なので、PC の中身は写らない。
- icon.png: res/app.ico の 256 px
- hero.png: 待機中と録画中の窓を並べた見出しの絵 (角は透明)
- idle / rec / paused / audio / shot / check / warn / saved / talk / settings.png: 各場面の窓 (2 倍・そのまま。settings は歯車の設定の画面)
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

STATES = ["idle", "rec", "paused", "audio", "shot", "check", "warn", "saved", "talk", "settings"]
FONT = "/System/Library/Fonts/ヒラギノ角ゴシック W5.ttc"


def rounded(im, r):
    mask = Image.new("L", im.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, im.size[0] - 1, im.size[1] - 1], r, fill=255)
    out = im.convert("RGBA")
    out.putalpha(mask)
    return out


def hero(idle, rec):
    W, win_w, gap, top = 1760, 780, 60, 64
    scale = win_w / idle.size[0]
    win_h = round(idle.size[1] * scale)
    H = max(680, top + win_h + 120)   # room for the label under the windows (the window grew in 0.4.0)
    left = (W - 2 * win_w - gap) // 2
    canvas = Image.new("RGBA", (W, H))
    top_c, bottom_c = (0x1A, 0x1F, 0x2A), (0x0D, 0x10, 0x16)
    grad = ImageDraw.Draw(canvas)
    for y in range(H):   # the window's graphite, a little lighter at the top
        t = y / (H - 1)
        grad.line([(0, y), (W, y)], fill=tuple(round(a + (b - a) * t) for a, b in zip(top_c, bottom_c)) + (255,))
    glow = Image.new("RGBA", (W, H))
    g = ImageDraw.Draw(glow)
    g.ellipse([left - 40, top - 60, left + win_w + 40, top + win_h + 60], fill=(0x38, 0xBD, 0xF8, 34))   # idle: ice cyan
    x2 = left + win_w + gap
    g.ellipse([x2 - 40, top - 60, x2 + win_w + 40, top + win_h + 60], fill=(0xE1, 0x1D, 0x48, 40))        # recording: crimson
    canvas = Image.alpha_composite(canvas, glow.filter(ImageFilter.GaussianBlur(70)))
    font = ImageFont.truetype(FONT, 26)
    for x, im, label in [(left, idle, "待機中"), (x2, rec, "録画中")]:
        shadow = Image.new("RGBA", (W, H))
        ImageDraw.Draw(shadow).rounded_rectangle([x, top + 16, x + win_w, top + 16 + win_h], 16, fill=(0, 0, 0, 170))
        canvas = Image.alpha_composite(canvas, shadow.filter(ImageFilter.GaussianBlur(26)))
        win = rounded(im.resize((win_w, win_h), Image.LANCZOS), 14)
        canvas.alpha_composite(win, (x, top))
        ImageDraw.Draw(canvas).rounded_rectangle([x, top, x + win_w - 1, top + win_h - 1], 14, outline=(0x2B, 0x32, 0x40, 255), width=1)
        d = ImageDraw.Draw(canvas)
        tw = d.textlength(label, font=font)
        d.text((x + (win_w - tw) / 2, top + win_h + 30), label, font=font, fill=(0x9A, 0xA4, 0xB2, 255))
    return rounded(canvas, 28)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    src, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    repo = Path(__file__).resolve().parents[1]
    ico = Image.open(repo / "res/app.ico")
    ico.size = (256, 256)
    ico.convert("RGBA").save(out / "icon.png", optimize=True)
    shots = {s: Image.open(src / f"pub-{s}.png").convert("RGB") for s in STATES}
    for s, im in shots.items():
        im.save(out / f"{s}.png", optimize=True)
    hero(shots["idle"], shots["rec"]).save(out / "hero.png", optimize=True)
    for f in sorted(out.glob("*.png")):
        with Image.open(f) as im:
            print(f"{f.name} {im.size[0]}x{im.size[1]} {f.stat().st_size // 1024} KB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
