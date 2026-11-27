# Implementation Plan for Issues #141-#150

## #141: [UX] 発振器の出力中表示を常時目立たせ、測定と発振器のショートカットを付けて画面に表示する
**Files:** BottomBar.cpp/h, LevelMeter.cpp/h, Strings.h
- Add persistent oscillator active indicator in bottom bar (always visible when noise is playing)
- Add keyboard shortcut hints on screen (Space=measurement, S=snapshot, R=reset, Esc=emergency stop, N=noise)
- Make oscillator button more prominent when active

## #142: [UX] Delay Finder の改善 検出して即適用・Track# 追従）IR クリップで持つ
**Files:** DelayIrTab.cpp/h, Strings.h
- Add auto-apply option for detected delay
- Track number following (auto-select system when delay measured)
- IR clip hold: keep IR display when measurement stops

## #143: [UX] スナップショット・トレース管理 色・名前・表示順・dB/遅延オフセット・トレース平滑化
**Files:** TransferFunctionTab.cpp/h, Core/Snapshot.h (if exists), Strings.h
- Add color picker for snapshots
- Add name editing for snapshots
- Add drag-to-reorder snapshot list
- Add dB offset control per snapshot
- Add delay offset control per snapshot
- Add per-snapshot smoothing override

## #144: [UX] 振幅・位相・コヒーレンスの3段表示、コヒーレンスによるブランキング、縦スケールの固定・プリセット
**Files:** FrequencyGraphView.cpp/h, Strings.h
- Already has 3-panel display (amplitude/phase/coherence) - verify
- Add coherence blanking toggle (already has threshold)
- Add fixed vertical scale presets (e.g., ±6dB, ±12dB, ±24dB, auto)
- Add dB scale lock/unlock button

## #145: [UX] カーソルとマーカー: トレース値の読み取り・差分・ピーク探索・マーカー固定
**Files:** FrequencyGraphView.cpp/h, Strings.h
- Add cursor readout (already has hover readout)
- Add fixed markers (click to place, show freq/dB/phase/coherence)
- Add delta between markers
- Add peak search (next peak, previous peak, highest peak)
- Add marker list/management

## #146: [UX] 入力メーターに dBFS の数値・目盛り・クリップ検知・解除を追加し、TF グラフ上でもクリップを警告する
**Files:** LevelMeter.cpp/h, BottomBar.cpp/h, FrequencyGraphView.cpp/h, Strings.h
- Add numerical dBFS readout on level meters
- Add scale markings on level meters
- Add clip detection with visual indicator
- Add clip clear button
- Show clip warning on TF graph when input clips

## #147: [UX] 平滑化の選択をプリセット化し、FIFO 平滑化追加する
**Files:** MeasurementPanel.cpp/h, Strings.h
- Convert smoothing combo to preset-based (1/48, 1/24, 1/12, 1/6, 1/3, 1/1, 2/1 oct + FIFO)
- Add FIFO smoothing option (exponential averaging with configurable time constant)
- Add custom smoothing entry

## #148: [UX] 破壊的な操作に確認ダイアログ・Undo を付ける
**Files:** LimitTestTab.cpp/h, TransferFunctionTab.cpp/h, DelayIrTab.cpp/h, Strings.h
- Add confirmation dialog for snapshot delete
- Add confirmation dialog for system delete
- Add confirmation dialog for loopback clear
- Add confirmation dialog for project clear
- Implement simple undo stack for: snapshot delete, system delete, delay clear, loopback clear

## #149: [UX] EQ タブに専用グラフを置き、ツールチップと操作発見性を改善する
**Files:** EQTab.cpp/h, Strings.h
- Add dedicated frequency response graph in EQ tab
- Show measured, target, and predicted curves
- Add tooltips on all controls
- Add inline help/hints for discoverability

## #150: [test] 既存テストが測定の正しさを証明できてないので回帰テストを追加する
**Files:** tests/ (new test files)
- Add transfer function measurement regression test
- Add delay estimation regression test
- Add SPL calibration regression test
- Add EQ proposal regression test
- Add smoothing regression test
- Add coherence calculation regression test