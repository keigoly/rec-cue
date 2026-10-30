# Rec Cue! for macOS — DEVELOPMENT.md

## 1. このディレクトリの役割

Rec Cue! の Mac 版のソース (Swift・SwiftPM)。Windows 版 (リポジトリ直下の `src/`・C++ / Win32) とコードは共有しない。
共有するのは、台詞 (`../res/shiori-lines.txt`。Mac で事実と違う台詞は `shiori-mac.txt` で除いて足す)・絵 (`../res/`)・設定とログの形 (1 行 1 JSON・`stats`・`sessions/<sid>.json`・`--verify-file`)・設計の記録 (`../docs/DESIGN.md`)。

- 対象は **macOS 15 以降**。画面・Mac の音・マイクを ScreenCaptureKit の 1 本の流れで、同じ時計 (ホストの時計) のまま取れる最初の版。
- 構成: ScreenCaptureKit (`Capture.swift`・取り込んだ絵を 3 枚まで取っておく) → 15 fps の刻みを 100 ms 遅らせて打ち、刻みの位置 + 半コマまでに取り込んだ最新の絵を書く (`Recorder.swift`・画面が変わらなくても同じ絵を書く。コマ数は Windows 版と同じく一定) → AVAssetWriter (`Writer.swift`・H.264 は VideoToolbox・AAC・2 秒ごとに断片を書く MP4)。音は Mac の音とマイクを 48 kHz の 1 本の時間軸に並べて混ぜる (`AudioMixer.swift`・150 ms 待ってから混ぜる・音が無い間は無音)。
- 置き場: 設定・ログ・セッションの記録は `~/Library/Application Support/Rec Cue/`、録画は `~/Movies/画面録画/` (Windows 版の「ビデオ\画面録画」に合わせた)。
- **いまは Step 2** (2026-10-27〜): 録画の芯 (コマンドライン)・一時停止・録音だけ・設定ファイル・スクショ・窓とメニューバー・しおりの台詞・窓の機能までできた。2026-10-28 に Step 2 と窓の分のコードレビューの指摘 (高・中) を直した (`../docs/DESIGN.md` 4.9)。2026-10-30 に Windows 版 0.5.0 の設定の画面を入れた (§2・`../docs/DESIGN.md` 4.11)。残りは本人の試用・軽い指摘 (§2 の最後)・main への取り込み。

## 2. 現在の問題点(2026-10-30 時点)

- 窓は Windows 版の A 案を同じ寸法と色で描く (`WindowView.swift`・`MainWindow.swift`)。録画・録音・一時停止・停止・スクショ (録画中は枚数とその時刻を記録に残す)・保存先を開く・ピン (`topmost` に覚える)、画面 / マイク / PC の音声 / 形式の選択 (config.ini を書き換える)、歯車の設定の画面 (保存先 2 つ: 欄で Finder・「変更」でフォルダーを選ぶ画面 (NSOpenPanel のシート)、ショートカットキーの表示、ログを取得 (`LogPack.swift`)・GitHub・公式サイト・版。← / 歯車 / Esc で戻り、録画を始めると戻る)、音量を確認 (`LevelCheck.swift`・録画と同じ取り込みとミキサーでメーターだけを動かし、何も書かない・60 秒で止まる・確認中にマイクや PC の音声を選び直すとやり直す・録画を始めると止まる)、録画中の警告 (帯の下の文字・琥珀色の帯・しおり: 画面を取り込めない・PC の音声が 1 分以上無音・マイクなし・PC の音声なし)、ショートカットキー (`Hotkeys.swift`・どのアプリを使っていても効く・既定 ⌃⌥R / ⌃⌥A / ⌃⌥S (2026-10-28 に 4 つのキーから 3 つに)・config.ini の `hotkey` / `audio_hotkey` / `shot_hotkey`・帯の見出しの下と録画中のカメラの横と設定の画面に表示)、説明の吹き出し、キーボードでの操作 (Tab / Shift+Tab・Space / Enter・↓ / F4) は動く。Windows 版の窓の機能で Mac 版に無いのは、タスクバーのボタン・画面の左上の丸・Alt+Space のメニュー (いずれも Mac には無いもの)。見た目は `--ui-snapshot <png> --ui-state idle|starting|rec|shot|paused|audio|saving|saved|warn|settings|settings-rec [--at 2026-10-22T14:00]` で確かめる (窓は出さない)。メニューバーのアイコンからも同じ操作ができる (`StatusItem.swift`)。
- **設定の画面 (2026-10-30)**: Windows 版 0.5.0 と同じ動き。自動の試験 (`--ui-autotest`) はフォルダーを選ぶ画面を通さず、選んだ後の処理 (書けるかの試し・既定なら空・config.ini) を呼ぶ。本人が手で確かめた (10-30): 切り替え (歯車・←)・欄で Finder (録画・スクショ)・「変更」で選ぶ画面から選ぶ (4.1 秒)・ログを取得 (ダウンロードに 256 ファイル 15.6 MB を 0.2 秒・Finder で選んで見せた。macOS の許可の確認は出なかった)。手で試していないのは、選ぶ画面の取りやめとスクショの「変更」(自動の試験では確かめた)。前の歯車のメニュー (設定ファイルを開く・ログを見る・Rec Cue! について) は、確かめた後に消した。書類やデスクトップを保存先に選んだときは、起動し直した後の最初の録画で macOS がそのフォルダーへのアクセスを確かめるはず (未確認。選んだ直後の書き込みの試しは、選ぶ画面を通したので確認なしで通る)。
- ScreenCaptureKit の開始がまれに長く止まる: 10-28 に 10.9 秒、10-30 の `--ui-autotest` で 26.5 秒 (音量の確認を続けて開け閉めした直後。その間に始めた取り込みとスクショがそろって待たされ、同時に動き出した)。前後の回は 0.5 秒前後。原因は未確認 (macOS 側か)。録画の開始に時間がかかっても、準備中のまま待つだけで録画は始まる。
- しおりの台詞は Windows 版と同じ選び方 (`Shiori.swift`・場面・時刻・曜日・季節・録画の種類・前に言った台詞を避ける)。台詞は `../res/shiori-lines.txt` を読み、`shiori-mac.txt` で Mac では事実と違う台詞 (タスクバー・MP3・画面の左上の丸など) を除いて足す。まだ Mac 版に無い機能 (音量を確認・警告・ショートカット・説明の吹き出し・キーボード) に触れる台詞も一時的に除いているので、機能を入れたら `shiori-mac.txt` の下の段から消す。
- ショートカットは Carbon の `RegisterEventHotKey` (アクセシビリティの許可は要らない) を「排他」で登録する。ほかのアプリが排他で持っている組み合わせは登録できず「(ほかのアプリが使用中)」と出す (2 つ起動して確かめた)。システム設定のキーボードショートカットとの重なりは検出できない。実際のキーを押しての確認はまだ (自動の試験は同じホットキーのイベントをアプリ自身に送る)。
- 「画面を取り込めていません」は、画面のロック・ほかのユーザーへの切り替え・ScreenCaptureKit のコマが空白か一時中断 (`blank` / `suspended`) のときに出す。実際にロックしたときの動き (コマが来なくなるのか、空白のコマが来るのか) は未確認。止まった画面はコマを送らないので、コマが来ないことだけでは警告しない。
- マイクが見つからないとき・マイクの許可が無いときは、録画を止めずにマイクなしで録る (Windows 版と同じ。窓は「マイクなし」/「マイクの許可がありません」と琥珀色で出し、sessions に `mic_problem`)。前は録画の開始ごと失敗していた。
- 音量の確認も ScreenCaptureKit で取り込むので、確認中は macOS の画面収録の表示 (メニューバーの印) が出る (Windows 版には無い)。実際のマイクと PC の音声でのメーターとしおりの声かけ (届いた・8 秒たっても無音) は、本人が試すまで未確認 (自動の試験は Rec Cue! 自身の音だけで、無音)。
- **まれに、録画を始めて 0.3 秒以内に、窓の文字を測る所 (macOS の CoreText) で例外が起きてアプリが落ちた** (2026-10-27・`--ui-autotest` 28 回に 1 回・原因は不明)。いまは文字ごとに例外を捕まえ、その文字だけを諦めてログに `ui.text` を書く (`Sources/RecCueObjC/`)。その後の 40 回では 0 回。
- 録画中のスクショが 50 回に 1 回、ScreenCaptureKit の「ストリーミングを開始できませんでした」で失敗した (2026-10-27)。失敗はしおりと知らせで伝わる。やり直すかは数を見て決める。
- 実画面とマイクは 15 秒だけ確認済み (2026-10-26・落としたコマ 0・Mac の音とマイクとも隙間 0・`../docs/DESIGN.md` 4.9)。長い録画・外部ディスプレイ・ヘッドセット・Bluetooth のマイクは未試験。ふだんの試験は Rec Cue! 自身が開く合成の模様の窓だけを録る方式 (`--test-pattern`)。
- 内蔵の Retina ディスプレイ (例 2940×1912) をそのままの画素数・2000 kbps で録る。画質・容量・CPU は試験の数値を見て決める (縮小するか・ビットレートを画素数に合わせるか)。
- **映像は音より平均 +44 ms 遅い (15 fps・2026-10-27 の同期の試験)**: 大半 (+36 ms 前後) は取り込みの段の分 (画面に出るまでの遅れと 15 fps に間引く分) で、録画の書き方では縮まらない。書き方の分は、刻みを遅らせて取り込んだ時刻で絵を選ぶように直して、平均 +25 ms (最大 +98) から半コマ以内になった。60 fps では全体で +28 ms。音の時刻は正確 (予定どおり)。前後の数値は `../docs/DESIGN.md` 4.9。
- `stats` の `proc` (CPU・メモリ) は Rec Cue! 本体だけ。圧縮 (VTEncoderXPCService・約 110 MB) と取り込み (replayd) は別のプロセスで動くので入らない。Windows 版と比べるときは `top` などで外から測る。
- ビルドは Apple シリコン (arm64) 用だけ。Intel の Mac 用は未対応。
- 署名は開発用の自己署名だけ。配布 (Developer ID・公証) は未対応。
- MP3 は書けない (macOS に MP3 のエンコーダーが無い・2026-10-26 に macOS 27.0.1 で確認)。Mac の録音だけは M4A と WAV にする。
- **途中で終わるときの経路は一部が未試験**: 通常の終わりと Ctrl-C / kill (`--term-at`) は確認済み。取り込みが止まったとき (メニューバーの停止・画面の抜き差し・スリープ)・書き出しの失敗・ディスク不足 (空き 500 MB 未満で止める) は、仕組みはあるが試していない。結果の `ended_by` と `complete` で分かる。どの理由で終わっても、窓は終わり始めた時点で保存中になる (`onStopping`・SIGTERM で確認)。準備中に止めた録画は取りやめて、ファイルを残さない (`rec.cancel`)。
- 画面の録画中は、システムのスリープに加えてディスプレイのスリープも止める (`PreventUserIdleDisplaySleep`・2026-10-28 本人の判断)。録音だけはシステムのスリープだけ。抑止を持つことは確かめたが、実際に画面が暗くならないことは、ディスプレイのスリープの時間を待つ試験をしていない。
- 設定の画面 (`monitor`) が無いとき (外部ディスプレイを外したなど) は、録画・録音だけ・スクショ・音量の確認・窓の表示とも主画面にする (Windows 版と同じ・`Devices.screenIndex`)。外部ディスプレイでの実機の確認はまだ。
- **コードレビュー (2026-10-28) の軽い指摘で未対応のもの**: 読めない config.ini を窓での設定の変更が既定の内容で上書きする (`Config.set`)・同じ項目が 2 行あると窓での変更が効かない (書くのは最初の行、読むのは最後の行)、WAV が 4 GB で止まると complete が false になり失敗と出る、保存中も経過時間が進む、しおりの所から Shift+Tab で「保存先を開く」を飛ばす、同じキーを 2 つの機能に設定すると 2 つ目が「ほかのアプリが使用中」と出る (Windows 版は録音のキーが勝つ)、`Hotkeys` に後始末 (deinit) が無い、`--ui-autotest` のキーの段の待ちに上限が無い、録画中に設定の画面を書き換えると窓の画面の行とスクショが録っている画面でなく設定を見る。条件しだいの指摘 (未確認): 文字の例外を捕まえた後の描画の状態、説明の吹き出しが出ない可能性、確認から録画へすぐ切り替えたときに 2 本の取り込みが同じマイクを開く、スクショが戻らないと以後のスクショが黙って無視される、最小化した窓が「窓を開く」で戻らない可能性、遅い保存先で WAV の書き込みが窓のメーターの読み取りを待たせる、音量の確認の取り込みが止まっても気づかない。
- 長い録画での時計のずれは未対応 (マイクの時計がずれると数分ごとに 10 ms ほどの継ぎ目。`audio.sources` の `gaps` / `overlaps` に出る)。3 チャンネル以上のマイクは先頭の 2 つを使う (未試験)。
- macOS 27 の SDK では `expectsMediaDataInRealTime` と `AVAssetWriterInputPixelBufferAdaptor` が非推奨。対象の最低の版を 27 以上に上げると警告でビルドが止まる。

## 3. バグ修正時の手順(一気に直さない)

### Step 1: 調査とログ追加(見える化)
- まずログを読む: `~/Library/Application Support/Rec Cue/logs/rec-cue-YYYYMMDD.log` (1 行 1 JSON。`sid`・`phase`・`ok`・`ms`・`msg`。録画中は 10 秒ごとに `stats` 行: `video` = 刻み・新しい絵・同じ絵・書いた数・落とした数・刻みの遅れ (`late_ticks`・後から書いた `caught_up`・予定からいちばん早く書いた `min_late_ms` (- は早い))・選んだ絵を取り込んだ時刻と刻みの位置の差 (`pick_ms`)、`video.sck` = ScreenCaptureKit が渡したコマの種類、`audio.sources` = 音源ごとの量・隙間・遅れて捨てた量・`peak_db`・`silent_pct`、`proc` = `footprint_mb` (アクティビティモニタの「メモリ」)・`cpu_pct` (全コアに対する割合)・`assertions` (画面の録画中は `PreventUserIdleDisplaySleep` と `PreventUserIdleSystemSleep`、録音だけは後者だけ)、`system.disk_free_mb`) と `sessions/<sid>.json` (`ended_by`: time / app / user / signal / stream / writer / disk / wav_full)。結果の `complete` は、映像のサンプル数が書いた数と同じで、音が終わりまで入っていること。終わった録画の芯が解放されると `rec.freed` (残りの数)、録画中の終了は `ui.quit_requested` → `ui.quit_reply` → `ui.quit`、準備中に止めた録画の取りやめは `rec.cancel`、設定の画面が無く主画面にしたときは `rec.screen` / `shot.screen`。歯車の設定の画面は `ui.view`・`ui.open_dir`・`ui.folder` (所要 ms・既定か・書けなかった理由・取りやめ)・`logs.export` (始めと、結果の JSON)・`logs.show`・`logs.wait` (終了するときに ZIP を 3 秒待っても終わらなかった)。
- ファイルは `--verify-file` で読み直す (サンプル数・最初と最後の時刻・`max_gap_s`・MP4 の箱の数と `truncated`)。ffprobe でも照らし合わせる。
- 再現は合成の試験から (§4 の試験 1〜3)。足りない計測はログに足してから原因を絞る。結果を確認してから Step 2 へ。

### Step 2: 原因箇所のみ最小限の改修
- 取り込み (`Capture.swift`)・刻みとまとめ役 (`Recorder.swift`)・音 (`AudioMixer.swift`)・書き出し (`Writer.swift`)・検証 (`Verify.swift`) のうち原因の箇所だけ直す。
- 同じ試験をもう一度回して数値で比べる。結果を確認してから Step 3 へ。

### Step 3: 周辺の整合性確認と改修
- この DEVELOPMENT.md・`../docs/DESIGN.md`・`../README.md` を合わせる。Windows 版と形をそろえたもの (ログ・`sessions`・`--verify-file`) を変えたら両方を見る。

## 4. 開発の作法

### ビルド (Mac・Xcode は不要)

```sh
cd mac
scripts/make-dev-cert.sh    # 初回だけ: 開発用の署名 (自己署名・専用のキーチェーン)
scripts/build.sh            # build/Rec Cue.app (警告 0 で通すこと)
```

- 画面収録の許可は署名に結びつく。開発用の署名が無いとアドホック署名になり、作り直すたびに許可を取り直すことになる。
- `scripts/build.sh` は `swift build -c release -Xswiftc -warnings-as-errors`。Command Line Tools だけの環境で出るリンカーの警告 (`…/CommandLineTools/Developer/… not found`) だけは除く。

### 動かす

```sh
scripts/run.sh --list-devices      # 画面・マイク・許可の状態
scripts/run.sh --request-access    # 初回: 画面収録の許可を求める
```

- 引数なしで開く (`open "build/Rec Cue.app"`) と画面のアプリとして動く。メニューバーの丸から操作する。録画中に終了すると、先に保存してから終わる。

- `scripts/run.sh` は `open` (LaunchServices) で起動する。端末から直に起動すると、画面収録の許可を求めるのが端末のアプリになってしまうため。
- 許可は「システム設定 → プライバシーとセキュリティ → 画面とシステムオーディオ録音」で Rec Cue! をオンにする。オンにした後の起動から有効。

### 試験

試験の録画は `~/Library/Application Support/Rec Cue/test/` に置き、数値を見たら消す。

1. **合成の窓だけ 10 秒** (画面の左下に小さな窓が出る。Rec Cue! 自身の窓だけを録るので、ほかの画面は写らない)
   `scripts/run.sh --record --seconds 10 --test-pattern --system auto --mic none --out "$HOME/Library/Application Support/Rec Cue/test/t1.mp4"`
   → `ok`・`verify` (映像 約 150 コマ・`max_gap_s` 0.067 前後)・`stats.final` (`dropped` 0・`cpu_pct`・`footprint_mb`)
2. **60 秒の連続**: 1 の `--seconds 60` → `stats` の `footprint_mb` と `cpu_pct` が横ばいか
3. **落ちても残るか**: `--seconds 15 --crash-at 7` → `--verify-file` で約 6 秒分が `ok`
4. **実画面とマイク** (本人の合図で・ファイルはその Mac から出さない): 先に `scripts/run.sh --request-access --mic auto` でマイクの許可を済ませてから、`--record --seconds 15 --system auto --mic auto` (画面を動かし、音を鳴らし、話してもらう) → `audio.sources` の `peak_db` と `silent_pct` で Mac の音と声が入ったかを見る

5. **映像と音のずれ** (スピーカーから音が鳴るので、そばにいる人に断ってから): `--record --seconds 15 --test-pattern --sync-test --system auto --mic none` (`--fps 60` でも) → `sync.video_minus_audio_ms` (`file` = ファイルの中の映像 − 音・`sck` = 取り込みの段・`added_by_recorder` = Rec Cue! の書き方で足された分。+ は映像が遅い)・`sync.file_vs_plan_ms`・`sync.found` (光と音を何回見つけたか)

6. **絵の時刻 (音なし)**: `--record --seconds 15 --test-pattern --still --sync-test --no-beep --system none --mic none` (`--fps 60` でも) → `sync.flash_file_minus_sck_ms` (ファイルの光 − 取り込んだ光) が半コマ以内か。`--still` は窓を動かさない (光るときだけ変わる)
7. **最後のコマ**: `--record --seconds 3 --test-pattern --system auto --mic none --end-snap-ms 0.5` (終わりを刻みの 0.5 ms 後に寄せる) → `complete` true・映像が書いた数と同じ (映画と映像の刻みが 1/600 秒だったころは毎回 1 コマ欠けた)
8. **Ctrl-C / kill で止める**: `--record --seconds 20 --test-pattern --term-at 5` → `ended_by: signal`・5 秒で `complete`
9. **一時停止**: `--record --seconds 10 --test-pattern --sync-test --system auto --mic none --pause-at 3 --pause-for 2` (音あり。`--no-beep --system none` なら音なし) → 長さが 2 秒短い・`max_gap_s` が 1 コマ以下・`sync.cut_by_pause` 2・止めた後の光と音も `file_vs_plan_ms` が止める前と同じ。コマの間隔の整数倍でない長さ (`--pause-for 2.03`) でも `video.min_late_ms` が 0 以上・`late_ticks` 0 (再開のたびに刻みを合わせ直す)。遅い fps の終わり: `--fps 1 --seconds 7.2 --pause-at 2 --pause-for 1.3` → `complete`
10. **録音だけ** (音が鳴る): `--record --audio-only --audio-format m4a --seconds 10 --test-pattern --sync-test --system auto --mic none` (`wav` でも・`--pause-at 3` でも) → `sync.file_vs_plan_ms.audio` が +2 ms 前後・`--crash-at 7` で約 6 秒分が再生できる
11. **スクショ**: `--screenshot --test-pattern --no-clipboard --out "…/test/s.png"` → 画面の大きさ・`visible_pct` が 9% 前後 (合成の窓だけ)。無い画面 (画面 1 枚の Mac で `--monitor 1`) でも主画面を撮り、ログに `shot.screen` (録画と録音だけも同じく主画面で録り `rec.screen`)。`--shot-selftest` (本人の合図で・保存しない) → `magenta_pct` が 0 前後。`--shot-selftest --include-self` (逆の確かめ) では 100 前後
12. **アプリとしての操作**: `--ui-autotest --out "…/test/u.mp4"` (4 本録って `…-warn.mp4`・`…-keys.mp4`・`…-self.mp4` も作る。同じ名前のファイルが残っていると開始に失敗するので、前の回の分は消しておく) → 各段の `recorders` (録画の芯の生きている数・待機中は 0) と最後の `recorders_at_end` 0・`first_click` true (ほかのアプリが前でも 1 回目のクリックが届く)・`self_end` (2 秒で SIGTERM を受けた録画が 0.1 秒後に `saving`・その間の ❚❚ が届かず `pauses` 0)・`dialogs` (Task の中から出したダイアログの間にほかの仕事が待たされた `other_job_ms` 0・窓を隠しているときのスクショの失敗で `hidden_shot_dialogs` 0・`window_shown` true。試験のダイアログは 1.5 秒で自分で閉じ、Rec Cue! を前に出さない)・`early_stop` (開始の直後に ■: 録画と WAV とも `cancelled`・`file_left` false・`dialogs` 0)。3 本目は時計の起点を決めた後に 300 ms 待つ (準備中に窓が時間を読んでも落ちないこと)。そのうえで キーボード (Tab の順・Shift+Tab・Space で音量の確認を始めて止める) と説明の吹き出しの文言 (`keys`) → 音量の確認 (ボタンで止める・2 秒にした時間で止まる・確認中に録画を始めて止まる。Rec Cue! 自身の音だけを聞き、マイクなし) → 録画 → スクショ → 一時停止 → 再開 → 停止を合成の窓・マイクなしで回し (スクショは Rec Cue! 自身の窓だけを撮る・クリップボードに入れない・同じフォルダーに PNG を置くので後で消す)、状態の移り変わり (`steps`・`shots` が 1)・`ended_by: app`・`complete`・`summary.screenshots` (PNG と `at_s`) を見る。ログの `ui.say` で、しおりの場面 (start → check → check_end → check → check_end → check → starting → rec → shot → pause → resume → saving → saved) が順に出たか (`check timed out` の `after_s` が 2.0〜2.1)。設定の画面の段 (`settings`・まとめて `settings_ok`): 歯車で開く (`opened`)・Tab の順 (`tab`)・保存先の変更 5 通り (自分のフォルダー / 録画と同じフォルダー / 書けない `/System` / 取りやめ / 既定。`folders_as_expected` がすべて true。config.ini は試験用のフォルダーの写しを使い、本物は書き換えない)・ログを取得 (`logs`・試験用のフォルダーに書いて消す・Finder は開かない)・Esc で閉じる・録画の開始で閉じる (`closed_by_start`)・録画中は「変更」が押せない (`while_recording`)。続く 2 本目は警告の試験 (無いマイクの名前・3 秒目から 2 秒だけ画面を取り込めないふり (`--screen-lost-at`)・無音の判定を 4 秒に短縮): `warnings` が マイクなし → 画面 → PC の音声無音 の順に琥珀色になり、しおりが 2 秒目・3 秒目・5 秒目に warn_mic → warn_screen → warn_sys を言うか。`warn_result` は `ok`・`complete`・`mic_problem: not found`窓が手前に出るがキーボードは奪わない。メニューバーにアイコンが数秒出る
13. **ショートカット**: `--hotkey-check` → 書き方の読み取り (`Ctrl+Option+R` / `Ctrl+Option+Shift+R` / `ctrl+alt+shift+a` / `⌃⌥⇧R` / `F13` / `none` / 読めない例) と、config.ini の 3 つが登録できるか。別の Rec Cue! が動いていると「ほかのアプリが使用中」になる。`--ui-autotest` の最後は、使われていないキー (⌃⌥⇧⌘F17〜F19) を登録し、同じホットキーのイベントで 録画 → 録音 (断られる) → スクショ → 停止
14. **しおりの台詞**: `--lines-check` → `problems` が空 (知らない場面・条件・差し込み、`shiori-mac.txt` で除く台詞が共有の台詞に無い)・`over` が空 (Mac の字体で吹き出しに入らない台詞)・`empty_picks` 0・`text_guard` true。台詞を変えたら必ず回す
15. **録画中の終了**: `--ui-quit-test --out "…/test/q.mp4"` (合成の窓を 2 秒録り、実行ループから終了を求める) → 結果の `quit_ms` が 0.3〜0.4 秒・`result.ok`・ログが `ui.quit_requested` → `rec.stop` → `ui.quit_reply` → `ui.quit`。直す前のように終わらないと `scripts/run.sh` が戻らないので、20 秒ほど待って残っていたら `pkill -KILL -f -- "MacOS/rec-cue --ui-quit-test"` で止める (SIGTERM は主キューが止まっていると効かない)
16. **電源の抑止**: 録画中に `pmset -g assertions` → 画面の録画は `PreventUserIdleDisplaySleep` と `PreventUserIdleSystemSleep` (理由「Recording the screen」)、録音だけは後者だけ (「Recording sound」)、止めた後は rec-cue の行が無い
17. **ログを取得**: `--export-logs "…/test/l.zip"` → `ok`・`files`・`skipped` 0・`ms`。`unzip -t` でエラー 0、展開した中身が `logs/`・`sessions/`・`config.ini` の元のファイルと同じか (今日のログは書き出した時点までの先頭部分。後に足されるのは `logs.cli` の行)。中身は実際のログなので、確かめたら消す


## 5. 関連ドキュメント

- `../DEVELOPMENT.md` — 全体と Windows 版 (ビルド・試験・公開の手順)
- `../docs/DESIGN.md` — 設計と測定 (Mac 版は 4.9 から)
- `../README.md` — 使い方
