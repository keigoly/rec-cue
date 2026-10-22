<p align="center"><img src="docs/images/icon.png" width="128" alt="Rec Cue! のアイコン"></p>

# Rec Cue!

[![stars](https://img.shields.io/github/stars/keigoly/rec-cue?style=flat&label=stars&color=38bdf8)](https://github.com/keigoly/rec-cue/stargazers)
[![license](https://img.shields.io/badge/license-MIT-38bdf8?style=flat)](LICENSE)
![Windows](https://img.shields.io/badge/Windows-10%20%2F%2011-0078D4?style=flat&logo=windows&logoColor=white)
![macOS](https://img.shields.io/badge/macOS-coming%20soon-555555?style=flat&logo=apple&logoColor=white)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat&logo=cplusplus&logoColor=white)
![Claude](https://img.shields.io/badge/Claude-D97757?style=flat&logo=claude&logoColor=white)

画面全体・マイク・PC の音声 (会議や通話の相手の声) を **1 本の MP4** に録る、Windows 向けの軽い録画アプリです。
画面は GPU のまま取り込んで GPU で H.264 にするので、録画中の CPU は 1〜2%、待機中は 0 です。単体の exe 1 つ (約 1.1 MB) で動きます。

A lightweight Windows screen recorder that puts the whole screen, your microphone and the PC's audio (the other side of a call) into one MP4. Capture and encoding stay on the GPU, so recording takes about 1–2% CPU and idling takes none.

> **Mac 版は準備中です (近日対応予定)。** 今は Windows 10 / 11 で動きます。

<p align="center"><img src="docs/images/hero.png" width="880" alt="待機中と録画中の画面"></p>

| 録音だけ (マイクと PC の音声を .m4a に) | 録画中のスクショ (Rec Cue! の窓は写りません) |
|---|---|
| ![録音中の画面](docs/images/audio.png) | ![スクショを撮った直後の画面](docs/images/shot.png) |
| **音量の確認 (会議の前に)** | **PC の音声が 1 分以上無音なら知らせる** |
| ![音量の確認の画面](docs/images/check.png) | ![警告の画面](docs/images/warn.png) |

<sub>画面の音量・機器名・フォルダーは見本です (`rec-cue.exe --ui-snapshot` で描いたもので、実際の PC の画面ではありません)。</sub>

## できること

- **1 本の MP4 に**: 画面全体 (H.264) と、マイクと PC の音声を混ぜた音 (AAC) を 1 本のファイルに録ります。自分の声も相手の声も同じファイルに入ります
- **録音だけ**: 画面を録らずに、マイクと PC の音声を .m4a に録ります
- **相手の声を録り損ねにくく**: PC の音声は「自動」で、既定のスピーカーと、アプリ (Teams・ブラウザー・ソフトフォンなど) が音を出している再生機器をすべて録り、録画中に増えた機器も足します。マイクも「自動」(ヘッドセットを優先) で、録画中に外れたら数秒で選び直します。PC の音声が 1 分以上無音のままなら知らせます
- **音量の確認**: 録る前に、マイクと PC の音声のメーターで音が来ているかを確かめられます
- **スクショ**: 待機中でも録画中でも、録画と同じ画面を PNG で保存し、クリップボードにもコピーします。**Rec Cue! の窓は写りません** (最前面に固定していても、後ろの画面がそのまま写ります)
- **最前面 (ピン)**: 窓をほかのアプリより手前に固定できます
- **録画中の印**: タスクバーのアイコンの右下で丸がゆっくり点滅し、画面の左上に小さな丸が点きます (録画は赤・録音は青)。どちらも録画とスクショには写りません
- **ショートカット**: `Ctrl+Alt+Shift+R` で録画、`A` で録音、`S` でスクショ (どのアプリを使っていても効きます)
- **落ちても残る**: 最初から本番のファイル (fragmented MP4) に 0.4 秒ごとに書き足すので、途中でアプリや PC が落ちても、そこまでの録画は再生できます。「一時ファイルに録ってから保存する」工程がないので、止めてから保存まで 1 秒もかかりません
- **案内役の湊しおり**: 右下の秘書が、場面に合わせて吹き出しでひとこと話します (台詞は 1,109 本)
- **記録**: 1 行 1 JSON のログに、録画中 10 秒ごとのコマ数・コマ落ち・音量・メモリ・CPU を残します

## 軽さ

| | 録画中 | 待機中 |
|---|---|---|
| CPU | 1〜2% | 0 |
| メモリ (private) | 約 180〜220 MB で横ばい | 約 40 MB |
| ファイル | 1 時間でおよそ 0.9 GB (1080p・15 fps・2 Mbps) | — |

<sub>Windows 11・Intel Core i7 のノート PC (内蔵 GPU Iris Xe)・1920×1080 で測定。exe は約 1.1 MB (アイコン・案内役の絵・台詞を含む)。</sub>

<p align="center"><img src="docs/images/how-it-works.svg" width="880" alt="録り方の図: 画面は GPU のまま H.264 に、マイクと PC の音声は 1 つの時計で混ぜて AAC に、どちらも 1 本の MP4 へ"></p>

- **画面は GPU の中だけで扱う**: 画面が変わったときだけ、Windows (DXGI Desktop Duplication) が新しい画像を GPU 上で渡します。それを GPU のメモリから出さずに色を変換し、GPU の動画専用の回路 (Intel Quick Sync など) で H.264 にします (Media Foundation)。CPU は画像に触れません
- **音は 1 つの時計に並べる**: マイクと PC の音声 (WASAPI のループバック) を、映像のコマと同じ時計 (QPC) の上に並べてから混ぜ、AAC にします
- **最初から本番のファイルに書く**: 0.4 秒ごとに区切って書く MP4 なので、止めたときは仕上げるだけ (約 10 ms) です
- **待機中は何もしない**: タイマーを止め、マウスや状態が変わったときだけ描き直します。録画中の受け渡しは決まった量のメモリを使い回すので、長く録っても増えません
- **Windows に最初から入っている部品だけ**: C++ で書いた 1 本の exe で、Electron・.NET・FFmpeg などの実行環境や外部のライブラリを使いません

## 案内役 湊しおり

| 待機中 | 録画中・録音中 |
|:---:|:---:|
| <img src="res/guide-idle.png" height="200" alt="冊子を両手で持つしおり"> | <img src="res/guide-rec.png" height="200" alt="冊子にペンで書き込むしおり"> |

右下の **湊 しおり** (みなと しおり) は、録画の段取りを手伝う秘書です。待機中は冊子を持ち、録画中と録音中は冊子に書き込んでいます。話し方は「きちんと秘書」(丁寧で控えめな敬語) です。

| 場面 | 台詞の例 |
|---|---|
| 開いたとき (時間帯・曜日・季節で変わる) | 今日も一日、記録のお手伝いをいたします。 |
| 録画中 | 録画中です。記録は私が見守っております。 |
| スクショ (何枚目か) | 3 枚目を保存いたしました。よい目印になります。 |
| 保存したとき (大きさ・長さ) | 保存いたしました (196.3 MB)。お疲れさまでございました。 |
| 警告 | PC の音声が静かなままです。スピーカーの選択をご確認ください。 |

- 台詞は 28 の場面に 1,109 本あり、時間帯・曜日・季節・録画の長さ・スクショの枚数などで選び分けます。同じ台詞は続けて出ません
- しおりか吹き出しをクリックすると、別のひとことを話します (待機中は使い方のヒントか雑談)
- 台詞を選ぶのは場面が変わったときだけです。台詞のために待機中に動くことはありません
- 台詞は [`res/shiori-lines.txt`](res/shiori-lines.txt) (1 行 1 台詞) にあり、ビルドで exe に入ります

## 動作環境

### Windows

- Windows 11 / Windows 10 バージョン 2004 以降 (64 ビット)。確認したのは Windows 11 です
- H.264 のハードウェアエンコーダーを持つ GPU を推奨します。確認したのは Intel の内蔵 GPU (Quick Sync) です。ハードウェアエンコーダーが無い PC では Windows のソフトウェアのエンコーダーで録る作りです (未確認・CPU を多く使います)
- Windows の N エディションでは、Media Feature Pack (Media Foundation) が必要です
- ランタイムのインストールは要りません

### Mac

準備中です (近日対応予定)。

## はじめ方

1. ソースからビルドします (下の「[ソースからビルド](#ソースからビルド)」)。ビルド済みの exe はまだ配布していません
2. `rec-cue.exe` を好きなフォルダーに置いて開きます。インストーラーはありません。設定とログは `%USERPROFILE%\.rec-cue\` に置かれます
3. 署名のない exe なので、初回は Windows の SmartScreen の警告が出ることがあります

## 使い方

1. 会議の前に **「音量を確認」** を押し、マイクのメーターが声で動くこと、相手が話したときに「PC の音声」のメーターが動くことを確かめます。確認中と録画中は、選択欄に「自動 → いま使っている機器名」が出ます
2. 中央の帯の左の **赤い丸** (または `Ctrl+Alt+Shift+R`) で録画を始め、もう一度押す (■ に変わっています) と保存します。録画中は時計の横に **REC** の札が出て、窓の上端に赤い線が引かれます
3. 保存先は `ビデオ\画面録画\Rec Cue 2026-10-22 193001.mp4` のような名前です (時刻は開始時刻)。「保存先を開く」で開きます

中央の帯は普段は半分ずつで、**左が録画** (画面・マイク・PC の音声)、**右が録音だけ** (マイクと PC の音声) です。押した側が伸びて、時間が出ます。録画中と録音中は、もう片方は押せません。

上の帯の右のアイコン (マウスを乗せると説明が出ます):

- **ピン**: 窓を最前面に固定します。次に開いたときも同じ状態です
- **カメラ**: スクショを撮ります。保存先は録画と同じフォルダーで、録画中に撮った枚数がアイコンの角に出ます。クリップボードの履歴 (Win+V) とクラウドのクリップボードには載せません
- **歯車**: 設定ファイルを開く / ログを見る / ショートカットキー (割り当てと、ほかのアプリが使っているか) / GitHub を開く / 公式サイトを開く / Rec Cue! について

キーボードでは Tab で項目を移り、Space か Enter で押せます (選択欄は ↓ でも開きます)。

録画中に画面がロックされたり管理者の確認画面が出たりした間は、映像は直前の画面のまま続き、音声は途切れずに録れます。ログオフやシャットダウンの前にはファイルを仕上げます。

### キー操作

| キー | 動作 |
|---|---|
| `Ctrl+Alt+Shift+R` | 録画の開始 / 停止 |
| `Ctrl+Alt+Shift+A` | 録音だけの開始 / 停止 |
| `Ctrl+Alt+Shift+S` | スクショ |

設定ファイルで変えられます (`none` で無効)。ほかのアプリが同じキーを使っているときは、歯車の「ショートカットキー」に出ます。

### 相手の声やマイクが入らないとき

- **PC の音声** は既定で「自動」です。選択欄を押すと一覧が開き、機器ごとに音を出しているアプリ名が `← ms-teams.exe` のように出ます。相手の声が出ている機器を選べば決め打ちできます
- **マイク** は既定で「自動」です。ヘッドセット → (ヘッドセットが仮想ミキサーに占有されているときは、音が来ているミキサーの出力) → 通話用の既定 → 既定 の順に選びます
- 録画中に「PC の音声が 1 分以上無音です」と出たら、相手の声が出ている機器を選び直してください

## 設定ファイル

`%USERPROFILE%\.rec-cue\config.ini` (歯車の「設定ファイルを開く」でメモ帳が開きます)。次の録画開始から反映されます。

| キー | 既定 | 内容 |
|---|---|---|
| `out_dir` | 空 = `ビデオ\画面録画` | 保存先 |
| `name_prefix` | `Rec Cue` | ファイル名の先頭 |
| `fps` | `15` | 1 秒あたりのコマ数 (1〜60) |
| `video_kbps` | `2000` | 映像のビットレート。1080p で 1 時間およそ 0.9 GB |
| `audio_kbps` | `128` | 96 / 128 / 160 / 192 |
| `monitor` | `0` | 0 = メインの画面 |
| `cursor` | `1` | マウスカーソルを映す |
| `mic` | `auto` | `auto` / `default` / `communications` / `none` / 機器名の一部 |
| `system` | `auto` | `auto` / `default` / `communications` / `none` / 機器名 (`\|` で複数) |
| `mic_gain_db` / `system_gain_db` | `0` | 音量の補正 (-20〜20) |
| `container` | `fmp4` | `fmp4` (落ちても再生できる) / `mp4` |
| `hw_encode` | `1` | GPU のエンコーダーを使う |
| `latency_ms` | `150` | 音声を揃える待ち時間。音が途切れるときだけ大きくする |
| `hotkey` | `Ctrl+Alt+Shift+R` | 録画の開始と停止。`none` で無効 |
| `audio_hotkey` | `Ctrl+Alt+Shift+A` | 録音だけの開始と停止。`none` で無効 |
| `shot_hotkey` | `Ctrl+Alt+Shift+S` | スクショ。`none` で無効 |
| `shot_dir` | 空 = 録画と同じ場所 | スクショの保存先 |
| `shot_clipboard` | `1` | スクショをクリップボードにもコピーする |
| `topmost` | `0` | 窓を最前面に固定する (画面のボタンで切り替えると書き換わる) |
| `github_url` | `https://github.com/keigoly/rec-cue` | 歯車のメニューの「GitHub を開く」 |
| `site_url` | `https://official.keigoly.jp/` | 歯車のメニューの「公式サイトを開く」 |

## ログ

- `%USERPROFILE%\.rec-cue\logs\rec-cue-YYYYMMDD.log` — 1 行 1 JSON。各処理の所要 ms・成否・エラーと、録画中 10 秒ごとの統計 (コマ数・コマ落ち・音量・メモリとその内訳・CPU・PC の空きメモリ)
- `%USERPROFILE%\.rec-cue\sessions\<セッション ID>.json` — 録画・録音ごとのまとめ (長さ・大きさ・機器・音量・コマ落ち・CPU とメモリの山・録画中に撮ったスクショ)

### 途中で落ちたファイル

fMP4 なので、Windows の「メディア プレーヤー」や VLC ではそのまま再生できます。
Mac の QuickTime などで長さが 0 秒に見えるときは `ffmpeg -i 落ちたファイル.mp4 -c copy 直したファイル.mp4` で直ります。

## ソースからビルド

Mac で、[llvm-mingw](https://github.com/mstorsjo/llvm-mingw) を使って Windows 用の exe を作ります (管理者権限は要りません)。

```sh
# ツールチェーン (初回のみ): llvm-mingw を home に展開
mkdir -p ~/.local/opt/llvm-mingw-dl && cd ~/.local/opt/llvm-mingw-dl
curl -fsSLO https://github.com/mstorsjo/llvm-mingw/releases/download/20261006/llvm-mingw-20261006-ucrt-macos-universal.tar.xz
tar -xJf llvm-mingw-20261006-ucrt-macos-universal.tar.xz && rm llvm-mingw-*.tar.xz

git clone https://github.com/keigoly/rec-cue.git
cd rec-cue
scripts/build.sh            # → build/rec-cue.exe (警告 0)
```

ツールチェーンを別の場所に置いたときは `LLVM_MINGW=<フォルダー> scripts/build.sh` です。ビルドの最初に、しおりの台詞の書き方と長さを `tools/check_lines.py` (Python 3) が確かめます。

### 試験

Windows で、合成の映像と音を使って確かめます (画面やマイクの中身は録りません)。

```bat
rec-cue.exe --record --seconds 8 --test-pattern --mic none --system none --verify --out %USERPROFILE%\.rec-cue\test\self.mp4 --json-out self.json
rec-cue.exe --record --seconds 60 --test-pattern --mic none --system none --verify --out %USERPROFILE%\.rec-cue\test\long.mp4 --json-out long.json
rec-cue.exe --ui-snapshot %USERPROFILE%\.rec-cue\test\ui.png --dpi 192
rec-cue.exe --lines-check --json-out lines.json
```

1 本目は 121 コマ・コマ落ち 0、2 本目はログの `stats` 行のメモリが増え続けないことを見ます。仮想ケーブル (VB-CABLE) があれば、`--system "CABLE Input" --test-tone "CABLE Input"` で相手の声の経路と音のずれも確かめられます。手順の詳細は [DEVELOPMENT.md](DEVELOPMENT.md) §5 にあります。

## 構成

| ファイル | 役割 |
|---|---|
| `src/main.cpp` | ウィンドウ (状態・マウスとキーボード・メニュー) と、試験用のコマンドライン |
| `src/recorder.cpp` | 1 回の録画のまとめ役 (映像のスレッド・10 秒ごとの統計・セッションのまとめ) |
| `src/video.cpp` | 画面の取り込み (DXGI Desktop Duplication・マウスカーソル・合成映像) |
| `src/writer.cpp` | 書き出し (Media Foundation の Sink Writer・H.264 と AAC・fragmented MP4) |
| `src/audio.cpp` | マイクと PC の音声 (WASAPI・1 つの時計に並べて混ぜる・機器の自動選択) |
| `src/shot.cpp` | スクショ (自分の窓を外して撮る・PNG とクリップボード) |
| `src/indicator.cpp` | 録画中の印 (タスクバーの点滅・画面左上の丸) |
| `src/ui_draw.cpp` | 画面の見た目 (Direct2D / DirectWrite で全部を描く) |
| `src/shiori.cpp` | しおりの台詞 (場面と条件で選ぶ) |
| `src/config.cpp` | 設定ファイル |
| `res/` | アイコン・案内役の絵・台詞・リソース定義 |
| `tools/` | 台詞の検査・アイコンと絵の加工・README の画像づくり (Python) |
| `scripts/build.sh` | ビルド |

設計と測定は [docs/DESIGN.md](docs/DESIGN.md)、進め方は [DEVELOPMENT.md](DEVELOPMENT.md) にあります。

## 制限

- 今は Windows だけです (Mac 版は準備中)
- 録れるのは 1 つの画面です (`monitor` で選びます)。HDR が有効な画面は黒く録れます
- Rec Cue! の窓そのものは録画に写ります (スクショには写りません)
- 録画ボタンを押してから録り始めるまで約 1.3 秒かかります (マイクの自動選択とエンコーダーの準備)
- 会議アプリとヘッドセットの組み合わせによっては、相手の声の機器を手で選ぶ必要があります。PC の音声の経路は、仮想ケーブルの試験音と既定のスピーカーで確かめています
- 画面読み上げ (スクリーンリーダー) には対応していません
- 署名のない exe です

## ライセンス

MIT ライセンスです ([LICENSE](LICENSE))。

アイコンと案内役の絵は NovelAI で作りました。

Windows・Direct2D・DirectWrite・Media Foundation・Teams・Snipping Tool は Microsoft Corporation の商標です。Intel・Quick Sync は Intel Corporation の商標です。macOS は Apple Inc. の商標です。そのほかの名称は、それぞれの権利者の商標です。本プロジェクトはこれらの企業とは関係ありません。
