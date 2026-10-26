#!/usr/bin/env python3
"""しおりの台詞 (res/shiori-lines.txt) を確かめる。Mac で動く (標準ライブラリだけ)。scripts/build.sh が最初に回す。

  python3 tools/check_lines.py res/shiori-lines.txt                # 場面ごとの数と、誤り・注意
  python3 tools/check_lines.py res/shiori-lines.txt --json out.json  # 一覧を JSON に (見本のページ用)

誤り (終了コード 1): 知らない場面・条件・差し込み / 条件だけで台詞がない / 同じ台詞が 2 回 / 吹き出しに入らない長さ。
長さは検証用 PC の DirectWrite で測った字幅 (Segoe UI Variable Text + 游ゴシック UI・11.5 DIP、2026-10-23) からの見積もりで、
差し込みは最も長い値 (12 時間 59 分・9999.9 MB・Ctrl+Alt+Shift+F12) に置き換えて測る。正確な字幅は
rec-cue.exe --lines-check (実際の吹き出しの幅 330.4 DIP) で確かめる。
"""
import json
import re
import sys
import unicodedata

SCENES = ["start", "back", "talk", "tip", "check", "check_mic", "check_sys", "check_quiet", "check_end", "starting",
          "rec", "rec_on", "audio", "audio_on", "shot", "shot_idle", "warn_screen", "warn_sys", "warn_mic", "warn_nosys",
          "recover", "saving", "saved", "fail_start", "fail_save", "fail_shot", "pin_on", "pin_off", "pause", "resume"]
TAGS = {"early", "morning", "noon", "afternoon", "evening", "night", "late",
        "sun", "mon", "tue", "wed", "thu", "fri", "sat", "weekday", "weekend",
        "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec",
        "spring", "summer", "autumn", "winter",
        "video", "audio", "short", "long", "shots", "auto", "newyear", "yearend", "clip"}
LONGEST = {"n": "100", "t": "12 時間 59 分", "mb": "9999.9", "shots": "100",
           "rec_key": "Ctrl+Alt+Shift+F12", "audio_key": "Ctrl+Alt+Shift+F12", "shot_key": "Ctrl+Alt+Shift+F12"}  # gitleaks:allow (ショートカットの見本・鍵ではない)
ROOM = 330.4    # 吹き出しの中の台詞の幅 (DIP・--lines-check の room_dip)
LIMIT = 322.0   # 見積もりの誤差を見込んだ上限
CASUAL = re.compile(r"(だよ|だね|じゃん|かな[。？]|よね|っす|ちゃう|[\U0001F300-\U0001FAFF])")

SMALL_KANA = set("ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮ")


def width(ch):
    """1 字の幅 (DIP)。仮名は実測 8.7〜9.6 を少し広めに見る。"""
    if ch in "「」・『』": return 5.8
    if ch in "。、": return 7.7
    if ch == "ー": return 7.5
    if ch == "…": return 8.5
    if ch in SMALL_KANA: return 8.0
    o = ord(ch)
    if 0x3041 <= o <= 0x30FF: return 9.7
    if ch == " ": return 3.2
    if ch in "il.,:;!|'": return 2.8
    if ch in "()[]": return 3.5
    if ch.isdigit(): return 6.2
    if "A" <= ch <= "Z": return 7.6
    if ch in "mwMW": return 10.0
    if "a" <= ch <= "z": return 5.9
    if ch in "+%=": return 9.4
    if o < 0x2000: return 6.5
    return 11.5   # 漢字・全角の記号


def measure(text):
    return sum(width(c) for c in text)


def longest(text):
    return re.sub(r"\{(\w+)\}", lambda m: LONGEST.get(m.group(1), m.group(0)), text)


def norm(text):
    """ほぼ同じ台詞を見つけるため、記号と空白を落とす。"""
    return "".join(c for c in unicodedata.normalize("NFKC", text) if unicodedata.category(c)[0] in "LN")


def parse(path):
    lines, errors = [], []
    scene = None
    with open(path, encoding="utf-8-sig") as f:
        for no, raw in enumerate(f, 1):
            s = raw.strip(" \t\r\n")
            if not s or s.startswith("#"):
                continue
            if s.startswith("[") and s.endswith("]"):
                scene = s[1:-1]
                if scene not in SCENES:
                    errors.append(f"{no}: 知らない場面 [{scene}]")
                    scene = None
                continue
            if scene is None:
                errors.append(f"{no}: 場面の見出しより前の行")
                continue
            tags, n = [], None
            while s.startswith("@"):
                tok, _, s = s.partition(" ")
                s = s.lstrip(" \t")
                t = tok[1:]
                if t.startswith("n=") and t[2:].isdigit():
                    n = int(t[2:])
                elif t in TAGS:
                    tags.append(t)
                else:
                    errors.append(f"{no}: 知らない条件 {tok}")
            if not s:
                errors.append(f"{no}: 条件だけで台詞がない")
                continue
            for name in re.findall(r"\{(\w*)\}", s):
                if name not in LONGEST:
                    errors.append(f"{no}: 知らない差し込み {{{name}}}")
            lines.append({"line": no, "scene": scene, "tags": tags, "n": n, "text": s})
    return lines, errors


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 2
    path = args[0]
    out = args[args.index("--json") + 1] if "--json" in args else None
    lines, errors = parse(path)
    notes = []
    seen, near = {}, {}
    for l in lines:
        t = l["text"]
        if t in seen:
            errors.append(f"{l['line']}: 同じ台詞が {seen[t]} 行目にもある: {t}")
        seen.setdefault(t, l["line"])
        k = norm(re.sub(r"\{\w+\}", "", t))
        if k in near and near[k][1] != t:
            notes.append(f"{l['line']}: {near[k][0]} 行目とほぼ同じ: {t}")
        near.setdefault(k, (l["line"], t))
        w = measure(longest(t))
        l["dip"] = round(w, 1)
        if w > LIMIT:
            errors.append(f"{l['line']}: 長すぎる (見積もり {w:.0f} / {LIMIT:.0f} DIP): {t}")
        if CASUAL.search(t):
            notes.append(f"{l['line']}: くだけた言い方: {t}")
    counts = {s: 0 for s in SCENES}
    for l in lines:
        counts[l["scene"]] += 1
    print(f"台詞 {len(lines)} 本  (最長の見積もり {max((l['dip'] for l in lines), default=0):.0f} DIP / 吹き出し {ROOM} DIP)")
    print("  " + "  ".join(f"{s} {c}" for s, c in counts.items()))
    for e in notes:
        print("注意 " + e)
    for e in errors:
        print("誤り " + e, file=sys.stderr)
    if out:
        with open(out, "w", encoding="utf-8") as f:
            json.dump({"count": len(lines), "scenes": counts, "lines": lines}, f, ensure_ascii=False)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
