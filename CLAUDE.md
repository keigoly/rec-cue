# Rec Cue! — Claude Code への指示

## 開発フロー

1. 変更は Step 1(見える化: まずログと `sessions/*.json` で問題の所在を数値で確かめる)→ Step 2(原因の箇所だけを最小限に直す)→ Step 3(周辺の整合: README の設定表・`config.cpp` の既定の ini・DEVELOPMENT.md を合わせる)の順。具体は `DEVELOPMENT.md` §4。
2. 新しいブランチは専用の worktree で作ることを勧める。

## 禁止事項

- 実画面・実マイクの録画ファイル・スクショ・ログ(`%USERPROFILE%\.rec-cue\`)を repo にコミットする。試験のファイルは合成の映像(`--test-pattern`)と合成の音(`--mic none`・`--test-tone`)だけで作る。
- 利用者の画面や PC の構成(機器名・フォルダー)が写る画像を README に載せる。README の画面は `--ui-snapshot`(音量・機器名・フォルダーが見本)で描く。
- Windows の既定の音声機器や、ほかのアプリ(ミキサー・通話アプリ)の設定を試験のために変える。

## このリポジトリ固有の指示

- 使い方は `README.md`、設計と測定は `docs/DESIGN.md`、ビルドと試験は `DEVELOPMENT.md` §5。
- ビルドは `scripts/build.sh`(llvm-mingw のクロスビルド・**警告 0**)。最初に `tools/check_lines.py` がしおりの台詞を確かめる。
- 変更したら試験 3 本(自己試験・仮想ケーブルの試験音・60 秒の連続)を回し、ログの数値で前後を比べる。画面を変えたら `--ui-snapshot`、台詞を変えたら `--lines-check`。
- 待機中の CPU 0 を保つ: 100 ms の刻みは録画中・確認中・保存中だけ動かす。待機中にタイマーを足さない。
- 台詞(`res/shiori-lines.txt`)は事実と合わせる: 使い方・警告の台詞は、その版の動きで言えることだけ。
