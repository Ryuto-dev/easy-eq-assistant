# 進捗メモ（Easy EQ assistant）

最終更新: 2026-11-26 / 対象コミット: `437cb18` 以降

このファイルは「どこまでできていて、何が残っているか」の引き継ぎ用。仕様は `Easy EQ assistant 仕様書.md` を参照。

---

## 1. 現在の到達点

| フェーズ | 状態 | 内容 |
|---|---|---|
| 1: デバイス/基本I/O | ✅ 完了・実機確認済 | 入力/出力の個別選択、Ref/Meas/Out ルーティング、レベルメーター、ピンクノイズ（フェード付）、出力上限ダイアログ、設定永続化 |
| 2: 伝達関数 | ✅ 完了・テスト済 | 実数FFT×2によるH1、Welch平均、コヒーレンス、ログ軸グラフ、スムージング、カーソル表示 |
| 3〜6: 上流取り込み | ⚠️ ビルド/テスト通過、実機検証はこれから | タブ構成、Delay/IR、ループバック補正、RTA/SPL、EQ案提示、ライブモード、ウォーターフォール、リミットテスト、ユーティリティ、会場/プロジェクト保存、エクスポータ |

- テスト: **43/43 パス**（`ctest --test-dir build --output-on-failure`）
- macOS 実機: マイク許可取得済み、Meas メーター動作、励振（ノイズ）.start/stop、SPL CSV ログ出力まで確認済み
- 直近の成果物タグ: `v0.2-phase2`（フェーズ2完了時）

### フェーズ3〜6で取り込んだ主なファイル

`src/UI/*Tab.cpp`（TransferFunction / DelayIr / RtaSpl / EQ / LimitTest / Utilities / Venue）、
`src/DSP/*`（DelayEstimator, EQProposer, ImpulseResponseBuilder, RtaSplAnalyzer, Waterfall, PEQ）、
`src/Core/{ProjectStore,Exporters,SystemCorrection,Utilities,Acoustics}.h`

---

## 2. macOS マイク許可（TCC）が出なかった原因と対策 — 重要

該当コミット: `437cb18` / `b75966a` / `e65f09f`

「許可ダイアログが出ない」「1秒で `DENIED by user` になる」という症状の原因は **4 つ重なっていた**。

| # | 原因 | 症状 | 対策 |
|---|---|---|---|
| 1 | `Info.plist` に `NSMicrophoneUsageDescription` が無い | マイク要求で即クラッシュ | JUCE の `MICROPHONE_PERMISSION_ENABLED/TEXT` で追加（`e65f09f`） |
| 2 | ロック画面中は TCC ダイアログを出せない | 要求が約1秒で「自動拒否」。ログの `DENIED by user` は**ユーザー操作ではない** | 実機確認は必ずロック解除中に行う |
| 3 | `AVCaptureDevice` 経由の許可要求 | 新しめの macOS ではダイアログが出ない | `AVAudioApplication`（macOS 14+）へ切替（`b75966a`） |
| 4 | **実行ファイル名に空白**（`CFBundleExecutable "Easy EQ assistant"`） | TCC の帰属処理が壊れ、ダイアログを出さず自動拒否（本体） | `PRODUCT_NAME "EasyEQAssistant"` ＋ 表示名は `cmake/display_name.plist` で維持 ＋ POST_BUILD でバンドル単位 ad-hoc 署名（`437cb18`） |

### 再現手順（デバッグ用）

```bash
tccutil reset Microphone com.ryuto.easyeqassistant
open build/EasyEQAssistant_artefacts/EasyEQAssistant.app
# → ロック解除中の状態で「許可」を選ぶ
```

ログ確認: `~/Library/Logs/EasyEQAssistant/eeq.log`（`mic permission: granted` / `audio opened: ...`）

### 環境メモ

- この Mac には Screen Time の制限プロファイル（`system.ratings.digital_health_restrictions.*`）が入っている。TCC の挙動に影響する可能性あり
- `profiles status -type enrollment` → DEP/MDM 登録なし

---

## 3. ビルド / テスト / 実行

```bash
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure        # 43 tests
open build/EasyEQAssistant_artefacts/EasyEQAssistant.app
```

- JUCE 8 Personal（ submodule `8.0.17-security`）
- ユニバーサルバイナリ（arm64 + x86_64）、macOS 12 以降
- エラーログ: `~/Library/Logs/EasyEQAssistant/eeq.log`

---

## 4. コード上の注意点（再発防止）

- **UI 文字列は `src/UI/Strings.h` に集約**。非 ASCII リテラルには必ず `u8` プレフィックス（JUCE の `String(const char*)` は ASCII 前提）
- **生成 `JuceHeader.h` は使わない**。`src/Core/JuceModules.h` を使う（`using namespace juce` により `MacTypes.h` の `Point` と衝突するため）
- Apple フレームワーク（IOKit / AVFoundation / AVFAudio）は **JUCE モジュールより先に** include する
- `std::atomic<std::shared_ptr>` はこの libc++ で未対応 → 励振バッファは `juce::CriticalSection` で保護
- オーディオスレッド内ではメモリ確保・ロック・ファイル I/O 禁止
- `TransferFunctionAccumulator` の実数FFT入力は **バッファ先頭の連続サンプル**（インターリーブ不可）

---

## 5. 残タスク（次の一手）

1. **仕様 7.2 の通し検証**: ノイズ測定 → スナップショット → EQ案 → 適用後の再測定（Before/After 比較）
2. **既知応答の自動検証**: 模擬系（既知のフィルタ等）を通して、測定値が既知の応答と一致するかを確認する自動化テストを追加
3. **ループバック校正**: 出力→入力ケーブルを挿した状態での遅延・校正動作確認
4. **フェーズ3〜6の取り込みコードの実機レビュー**: コンパイルは通るが UI 挙動は未検証
5. **Windows/ASIO**（フェーズ6）: `libs/asio_sdk/` に SDK を置いて WASAPI/ASIO 両対応
6. **アイコン**: `tools/make_icon.swift` で生成済み（`assets/icons/icon_1024.png`）

---

## 6. リポジトリ

- 遠隔: `https://github.com/Ryuto-dev/easy-eq-assistant`（private） / ブランチ `main`
- タグ: `v0.2-phase2`
