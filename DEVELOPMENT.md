# Rec Cue! — DEVELOPMENT.md

## 1. このディレクトリの役割

画面全体・マイク・PC の音声 (通話や会議の相手の声) を 1 本の MP4 に録る、Windows 向けの軽い録画アプリ **Rec Cue!** のソース。
C++17 / Win32 の単体 exe (ランタイムのインストールは不要)。Mac 上で llvm-mingw によりクロスビルドする。Mac 版は準備中。
2026-10-20 に Snipping Tool の録画が 2 本続けて保存に失敗したことを受けて作った (経緯と測定は `docs/DESIGN.md`)。

## 2. 開発環境

- ビルドは Mac (llvm-mingw のクロスビルド・§5)。試験は Windows 10 / 11 の PC で、合成の映像と音を使う (§5)。
- 公開用のリポジトリ (https://github.com/keigoly/rec-cue) は、非公開の開発用のリポジトリから書き出して更新している。

## 3. 現在の問題点(2026-10-23 時点)

- **実際の会議・通話での録画は未試験。** 相手の声 (ループバック) は仮想ケーブルに流した試験音と、既定のスピーカーで鳴った音で確認済み。実会議で相手の声が入るかは、最初の実会議の `sessions/*.json` の `audio.sources` (sys の `peak_db` と `silent_pct`) で確かめる。
- **ヘッドセットをつないだ状態は未試験。** 仮想ミキサーがヘッドセットを占有している場合は「音が来ているミキサーの出力」を選ぶ設計だが、実機では確かめていない。録画中にマイクが外れたら 5 秒ごとに選び直す (`audio.mic_replaced`)。
- **メモリ**: 録画中は 182〜217 MB で横ばい (60 秒の連続試験・0.3.3 の窓からの 2 分の録画)。大半はエンコーダーと GPU の受け渡し用バッファ (この PC の内蔵 GPU では GPU のテクスチャも private に数えられる)。10-22 に 0.2.0 の窓からの録画 (6 分・8 分) で毎秒 2〜4 MB 増えた件は 0.3.3 では再現しない (原因は未特定・`docs/DESIGN.md` 4.6)。普段の作業の 20〜30 分の録画で横ばいを確かめるのが次の一手。減らすなら BGRA→NV12 の変換を自前 (ID3D11VideoProcessor) にしてバッファを小さくする。
- アプリ自体が録画中に落ちると、fMP4 のトラック長の書き戻しが行われず、Mac (AVFoundation) では 0 秒に見える (Windows・ffmpeg では再生可・`ffmpeg -c copy` で直る)。ログオフ・シャットダウンでは先に保存する (`WM_ENDSESSION`)。起動時の自動修復は未実装。
- `IMFSinkWriter::Finalize` にタイムアウトが無い (エンコーダーが固まったときに「保存中…」のまま戻らない可能性)。録画中の固まりは 5 秒で自動停止する (`session.encoder`)。
- 画面のロック中・UAC の確認画面の間は映像が止まる (音声は続く)。解像度が変わったら録画の大きさのまま切り取りか黒の余白で続ける (`video.size_changed`)。どちらも実機の試験は未実施。
- 署名なしの exe (初回に SmartScreen の警告が出ることがある)。
- 画面 (0.2.0〜) は Direct2D / DirectWrite で全部を自前で描く (子コントロールなし・`src/ui_draw.cpp`)。マウスとキーボード (Tab / Space / Enter / ↓) は描画と同じ矩形で当たり判定する。画面読み上げには対応していない。0.3.0 は Gemini 4 Argon の A 案 (Executive Dark) と右下の SD の案内役。
- スクショで Rec Cue! の窓が写らないこと (`WDA_EXCLUDEFROMCAPTURE`) は `--shot-selftest` で確認済み (10-22)。外せない古い Windows では窓を一瞬隠して撮る (ちらつく)。
- タスクバーの点滅と画面左上の丸は実機で目視済み (10-22)。点滅はタスクバーの「バッジを表示する」設定が切れていると出ない。ログの `ui.taskbar` / `ui.overlay` / `ui.dot` で出したかが分かる。
- 待機中のメモリは working set 約 60 MB (private 約 40 MB)。GPU で描くため最初の版 (19 MB) より多い。待機中の CPU は 0 (100 ms の刻みは録画中・確認中・保存中だけ動く)。
- 録画ボタンを押してから録り始めるまで約 1.3 秒かかる (マイクの自動選択 0.5 秒 + エンコーダーの準備 0.7 秒)。その間の画面と音は入らない。

## 4. バグ修正時の手順(一気に直さない)

### Step 1: 調査とログ追加(見える化)
- まずログを読む: `%USERPROFILE%\.rec-cue\logs\rec-cue-YYYYMMDD.log` (1 行 1 JSON。`sid` = セッション ID・`phase`・`ms`・`ok`・`hr`・`msg`。録画中は 10 秒ごとに `stats` 行。0.3.3 からは `mem` = メモリの内訳・`ui` = 窓の描画などの回数・`video.cursor` = ポインターの描画も) と `sessions\<sid>.json`。メモリの内訳の見方は `docs/DESIGN.md` 4.6。
- 音の経路は `--probe-levels` (全マイクと全再生機器の音量を同時に測る) と `--list-devices` (機器・既定・音を出しているアプリ・H.264 エンコーダー) で見える化する。
- 再現は合成の試験から: `--record --test-pattern` (画面の代わりに合成映像)・`--test-tone "CABLE Input"` (部屋に聞こえない仮想ケーブルに試験音)。
- 足りない計測はログに足してから原因を絞る。結果を確認してから Step 2 へ。

### Step 2: 原因箇所のみ最小限の改修
- 映像 (`src/video.cpp`)・書き出し (`src/writer.cpp`)・音声 (`src/audio.cpp`)・まとめ役 (`src/recorder.cpp`)・画面の状態と入力 (`src/main.cpp`)・画面の見た目 (`src/ui_draw.cpp`) のうち原因の箇所だけ直す。
- 同じ試験をもう一度回して数値で比べる (§5 の試験 3 本)。結果を確認してから Step 3 へ。

### Step 3: 周辺の整合性確認と改修
- README の設定表・`config.cpp` の既定の ini・この DEVELOPMENT.md を合わせる。
- 動作確認が終わるまで旧経路 (例: `container=mp4`・`hw_encode=0` のソフトウェア経路) は残す。

## 5. 開発の作法

### ビルド (Mac)
```bash
# ツールチェーン (初回のみ・管理者権限不要): llvm-mingw を home に展開
mkdir -p ~/.local/opt/llvm-mingw-dl && cd ~/.local/opt/llvm-mingw-dl
curl -fsSLO https://github.com/mstorsjo/llvm-mingw/releases/download/20261006/llvm-mingw-20261006-ucrt-macos-universal.tar.xz
tar -xJf llvm-mingw-20261006-ucrt-macos-universal.tar.xz && rm llvm-mingw-*.tar.xz

scripts/build.sh            # → build/rec-cue.exe (警告 0 で通ること)
```
別の場所に置いたツールチェーンは `LLVM_MINGW=<dir> scripts/build.sh`。ビルドの最初に `tools/check_lines.py` がしおりの台詞を確かめる (Python 3 の標準ライブラリだけ)。

### 試験 (Windows)
`rec-cue.exe` の引数で回し、結果は `--json-out <file>` の JSON とログで見る。変更したら回す (1〜3 は 0.1 からの 3 本)。
1. 自己試験: `--record --seconds 8 --test-pattern --mic none --system none --verify --out %USERPROFILE%\.rec-cue\test\self.mp4` → 121 コマ・コマ落ち 0・`verify.ok`
2. 相手の声の経路: `--record --seconds 8 --test-pattern --mic none --system "CABLE Input" --test-tone "CABLE Input" --verify --out ...\tone.mp4` → `tone.offset_ms` が 3 つとも同じくらいの値 (2026-10-20 は 113〜117 ms) で揃うこと (仮想ケーブル VB-CABLE が要る)
3. 連続: `--record --seconds 60 --test-pattern --mic none --system none --verify ...` → `stats` 行の `private_mb` が増え続けないこと
4. 録音だけ (0.3.0〜): `--record --seconds 8 --audio-only --mic none --system "CABLE Input" --test-tone "CABLE Input" --verify --out ...\audio.m4a` → `audio_only:true`・`verify` の streams が audio だけ・`tone.offset_ms` が揃うこと
5. スクショ (0.3.0〜): `--shot ...\shot.png` → `capture_ms` / `save_ms` / `bytes`。実画面が写るので、試験の PNG は試験の後に消す。窓が写らないことは `--shot-selftest` (`in_shot_without_window` が 0・`in_plain_shot` が 9216 前後。画面の左上に 96 px の四角が 0.2 秒ほど出る)。

- 試験のファイルは合成の映像 (`--test-pattern`) と合成の音 (`--mic none`・`--test-tone`) だけで作る。実画面・実マイクを録った試験のファイルは試験の後に消す。
- 試験の録画も `sessions\<sid>.json` に残る。利用者の録画だけを拾うときは `path` が `\.rec-cue\test\` のものを除く。
- 画面の見た目は `--ui-snapshot %USERPROFILE%\.rec-cue\test\ui.png [--dpi 192] [--at 2026-10-22T14:00]` → `ui-idle / hover / check / rec / shot / warn / audio / opening / saved / talk.png` の 10 枚 (opening は録音の側が開く途中、talk はしおりにマウスを乗せてヒントを話すところ)。ウィンドウも音声機器も開かず、音量・機器名・フォルダーは見本なので、PC の中身は写らない。台詞は毎回同じ乱数で選び、`--at` でその日時の台詞 (時間帯・曜日・季節) にする。`--dpi 192` は 2 倍の絵。
- README の画像: `--ui-snapshot <dir>\pub.png --dpi 192 --at 2026-10-22T14:00` の 10 枚を Mac に持ってきて `uv run --with pillow python3 tools/make_readme_images.py <dir> docs/images` (アイコン・見出しの絵・各場面)。仕組みの図は `docs/images/how-it-works.svg` (手で書いた SVG)。
- しおりの台詞を変えたら `--lines-check` → 台詞の数 (場面ごと)・読み込みの ms・増えたメモリ・1 回選ぶ µs と、実際の字幅 (DirectWrite) で吹き出し (330.4 DIP) に入らない台詞 (`over`) と最も長い 12 本 (`widest`)。差し込みは最も長い値 (12 時間 59 分・9999.9 MB・Ctrl+Alt+Shift+F12) で測る。2026-10-23 (0.3.2): 1,109 本・0.95 ms・180 KB・0.44 µs・入らない台詞 0・最長 298.9 DIP。

### アイコンと案内役の絵 (Mac)
- アイコン: `uv run --with pillow python3 tools/make_icon.py res/icon-src.png res/app.ico --preview <確認用.png>` (角丸の四角・16〜256 px の 10 サイズ)。原画 `res/icon-src.png` は NovelAI V5 の S-L1P1 (seed 26102201) を 512 px にしたもの。
- 案内役 (SD): `res/guide-idle.png` (SD-P1b・seed 26102212) と `res/guide-rec.png` (SD-P2a・seed 26102213)。NovelAI の白い地を `uv run --with pillow --with numpy python3 tools/cutout.py <in.png> <out.png> --height 256` で透過にしたもの。exe に RCDATA (101 / 102) で入り、表示の大きさ (96 DIP) に WIC で縮めてから描く。

### しおりの台詞 (Mac)
- `res/shiori-lines.txt` (UTF-8・1 行 1 台詞・`[場面]` の見出しと `@条件`。書き方はファイルの先頭) を直して `scripts/build.sh`。ビルドの最初に `tools/check_lines.py` が回り、知らない場面・条件・差し込み、同じ台詞の重複、長すぎる台詞 (Windows 11 の DirectWrite で測った字幅からの見積もりで 322 DIP 超) があれば止まる。`python3 tools/check_lines.py res/shiori-lines.txt` で場面ごとの数、`--json <file>` で一覧。
- 台詞は事実と合わせる (使い方・警告の台詞は、その版の動きで言えることだけ)。録画には Rec Cue! の窓が写る (写らないのはスクショ・ツールチップ・左上の丸)。Rec Cue! はスリープを止めない。
- 実際の字幅は Windows で `--lines-check` で確かめる (上の「試験」)。

## 6. 関連ドキュメント

- `README.md` — 使い方・設定・ログ
- `docs/DESIGN.md` — 設計、経緯 (Snipping Tool が失敗した原因)、測定値
