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

## テスト

```sh
cmake --build build --target EasyEQTests
ctest --test-dir build --output-on-failure
```

## メモ

- Windows ビルドはフェーズ6 で確認。ASIO 対応には Steinberg の ASIO SDK が別途必要（規約によりリポジトリへはコミット不可。`libs/asio_sdk/` に配置してビルドする想定）
- ライセンス: JUCE 8 Personal（個人利用・非商用・非配布）
