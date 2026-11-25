# Easy EQ assistant

PA・音響調整用の伝達関数測定ソフト（個人用）。詳細は `Easy EQ assistant 仕様書.md` を参照。

- 言語 / FW: C++20 + JUCE 8（`libs/JUCE` は git submodule で固定）
- ビルド: CMake（macOS はユニバーサルバイナリ arm64 + x86_64、macOS 12 以降）
- テスト: GoogleTest（`ctest`）

## ビルド（macOS / コマンドライン）

```sh
cmake -B build -G Ninja
cmake --build build
open "build/EasyEQAssistant_artefacts/Easy EQ assistant.app"
```

Xcode 本体をインストール済みなら、`-G Xcode` で Xcode プロジェクトを生成してデバッグもできる。

## ビルド（Windows / Visual Studio 2022）

Visual Studio 2022 BuildTools 同梱の CMake を使う（PATH に CMake があれば `cmake` でよい）:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
# 実行ファイル: build\EasyEQAssistant_artefacts\Release\Easy EQ assistant.exe
```

**重要（日本語パスの回避）**: プロジェクトのパスに日本語（`開発` など）が含まれると、
JUCE のアイコン生成ツール（juceaide）が静かに失敗して CMake Configure が通りません。
ドライブ文字を割り当てて ASCII パスからビルドしてください（実体は同じ場所です）:

```powershell
subst X: "C:\Users\ryuto\開発\easy-eq-assistant"
cmake -S X:\ -B X:\build -G "Visual Studio 17 2022"
cmake --build X:\build --config Release
```

（`subst` は再起動で外れるので、ビルドし直すときは再度実行する）

## テスト

```sh
cmake --build build --target EasyEQTests
ctest --test-dir build --output-on-failure
```

## メモ

- UI 文字列は `src/UI/Strings.h` に集約する。非 ASCII を含むリテラルには必ず `u8` プレフィックスを付ける（JUCE の `String(const char*)` は ASCII 前提のため文字化けする）。MSVC では `/utf-8` を付けてある（CMakeLists）
- アプリのアイコンは `tools/make_icon.swift`（CoreGraphics 描画）で生成する: `swift tools/make_icon.swift assets/icons/icon_1024.png`
- 初回起動時にマイクへのアクセス許可を求められる（macOS の標準ダイアログ）。許可しないと入力メーターは動かない。Windows にはアプリごとの承認ダイアログがないため、OS 側の設定に従う（`src/Core/MicPermissionStub.cpp`）
- ASIO 対応（Windows・任意）: Steinberg の ASIO SDK を `libs/asio_sdk/` に展開すると自動で有効になる（規約によりリポジトリへはコミット不可）。ない場合は WASAPI で動く
- ライセンス: JUCE 8 Personal（個人利用・非商用・非配布）
