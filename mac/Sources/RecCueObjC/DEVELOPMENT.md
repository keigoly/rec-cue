# Sources/RecCueObjC — DEVELOPMENT.md

## 1. このディレクトリの役割

Swift からは捕まえられない Objective-C の例外を捕まえる小さな関数 `RCCatch` だけを置く (`include/RCCatch.h` が公開する宣言)。
Swift の本体 (`../RecCue/`) はこれを窓の文字の測りと描きにだけ使う (`WindowView.draw(_:_:align:)`・`pathWell`)。

## 2. 現在の問題点(2026-10-27 時点)

- 2026-10-27 の窓の操作の試験 (`--ui-autotest`) で 28 回に 1 回、録画を始めて 0.3 秒以内に、メーターの文字を測る
  `NSAttributedString.size()` の中 (CoreText の `TAttributes::ApplyFont`) で `NSInvalidArgumentException`
  (`attempt to insert nil object from objects[2]`) が起き、アプリが落ちた。原因は分かっていない (同じ時刻に別の
  プロセスからのアクセシビリティの問い合わせの中でも例外が起き、HIServices が飲み込んでいた)。
- いまは 1 つの文字列の描画だけを諦めて (その回はその文字が出ない)、`ui.text` の行 (例外の名前と理由・文字列・字体) を
  ログに 1 度だけ書く。録画は止まらない。原因が分かったら、ここを外せるか見直す。

## 3. バグ修正時の手順(一気に直さない)

1. Step 1: ログの `ui.text` (`ok: false`) を集め、どの文字列・字体・状況で起きるかを数える。
2. Step 2: 原因の箇所だけ直す (例: 字体の作り方)。`--ui-autotest` を何十回か回して、`ui.text` が出なくなったかを数値で比べる。
3. Step 3: `RCCatch` が要らなくなったら外し、この DEVELOPMENT.md と `../../DEVELOPMENT.md` を合わせる。

## 4. 関連ドキュメント

- `../../DEVELOPMENT.md` (Mac 版の作法と試験)
- `../../../docs/DESIGN.md` 4.9 (Mac 版の測定の記録)
