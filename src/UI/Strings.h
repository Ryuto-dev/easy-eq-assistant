#pragma once

#include "Core/JuceModules.h"

namespace eeq::strings
{
// すべての UI 文字列はこのファイル（またはこの単位）に集める。
// 日本語のみで開発するが、将来の英語翻訳に備えて一元管理する（検討事項の決定事項）。
//
// 注意: JUCE の String(const char*) は ASCII 前提で、127 を超えるバイトを
// 1バイト=1文字として扱う（文字化けの原因）。非 ASCII を含むリテラルには
// 必ず u8 プレフィックスを付け、C++20 の char8_t 版コンストラクタを使う。

inline const juce::String appName { u8"Easy EQ assistant" };

// 上部バー
inline const juce::String deviceLabel { u8"デバイス" };
inline const juce::String sampleRateLabel { u8"サンプルレート" };
inline const juce::String bufferSizeLabel { u8"バッファ" };
inline const juce::String refLabel { u8"Ref" };
inline const juce::String measLabel { u8"Meas" };
inline const juce::String outLabel { u8"出力" };
inline const juce::String channelNone { u8"なし" };
inline const juce::String internalReference { u8"内部参照" };
inline const juce::String notSelected { u8"-" };

// 下部バー
inline const juce::String startNoiseButton { u8"ピンクノイズ開始" };
inline const juce::String stopNoiseButton { u8"停止" };
inline const juce::String outputLevelLabel { u8"出力レベル" };

// ダイアログ・状態表示
inline const juce::String overLimitTitle { u8"出力レベルの確認" };
inline const juce::String overLimitMessage
    { u8"−20 dBFS を超える出力は、聴覚や機器に負担をかける可能性があります。このレベルで使用しますか？（ピンクノイズは実効値に対してピークが約 14 dB 大きいため、出力レベルの上限はクリップしない値に制限されています）" };
inline const juce::String overLimitOk { u8"このレベルで使用" };
inline const juce::String overLimitCancel { u8"−20 dBFS に戻す" };
inline const juce::String deviceErrorTitle { u8"オーディオデバイスのエラー" };
inline const juce::String deviceErrorMessage
    { u8"デバイスでエラーが発生したため、出力を停止しました。デバイスを選び直してください。" };
inline const juce::String micDeniedTitle { u8"マイクへのアクセスが許可されていません" };
inline const juce::String micDeniedMessage
    { u8"システム設定 ＞ プライバシーとセキュリティ ＞ マイク で Easy EQ assistant をオンにすると、入力（Meas）が動作します。" };
inline const juce::String noDeviceInfo { u8"デバイス未接続" };
inline const juce::String okButton { u8"OK" };
inline const juce::String degreeSymbol { u8"°" };
inline const juce::String gammaSquared { u8"γ²" };

// フェーズ2
inline const juce::String measurementStartButton { u8"測定開始" };
inline const juce::String measurementStopButton { u8"測定停止" };
inline const juce::String resetAveragesButton { u8"平均リセット" };
inline const juce::String graphPlaceholder { u8"測定開始でカーブが表示されます" };
inline const juce::String measurementPanelTitle { u8"測定設定" };
inline const juce::String fftSizeLabel { u8"FFTサイズ" };
inline const juce::String windowLabel { u8"窓関数" };
inline const juce::String averagingModeLabel { u8"平均方式" };
inline const juce::String averagingCountLabel { u8"平均回数" };
inline const juce::String smoothingLabel { u8"スムージング" };
inline const juce::String averagingModeCumulative { u8"累積平均" };
inline const juce::String averagingModeExponential { u8"指数平均" };
inline const juce::String noSmoothingItem { u8"なし" };
inline const juce::String inputDeviceLabel { u8"入力デバイス" };
inline const juce::String outputDeviceLabel { u8"出力デバイス" };

// ---- タブ・共通 ----
inline const juce::String tabTransferFunction { u8"伝達関数" };
inline const juce::String tabDelayIr { u8"IR・遅延" };
inline const juce::String liveMeasurementLabel { u8"測定中（ライブ）" };
inline const juce::String tabRtaSpl { u8"RTA・SPL" };
inline const juce::String tabLiveMode { u8"本番モード" };
inline const juce::String tabEQ { u8"EQ案" };
inline const juce::String tabLimitTest { u8"上限測定" };
inline const juce::String tabUtilities { u8"ツール" };
inline const juce::String tabVenue { u8"会場" };
inline const juce::String snapshotButton { u8"スナップショット" };
inline const juce::String delayMeasureButton { u8"遅延測定" };
inline const juce::String calibrationButton { u8"校正" };
inline const juce::String excitationLabel { u8"励起信号" };
inline const juce::String excitationPinkNoise { u8"ピンクノイズ" };
inline const juce::String excitationSweep { u8"ログスイープ" };
inline const juce::String excitationWav { u8"WAV再生" };
inline const juce::String excitationExternal { u8"外部入力のみ" };
inline const juce::String excitationCalTone { u8"1 kHz トーン（校正）" };
inline const juce::String excitationCalBandNoise { u8"1 kHz 帯域ノイズ（校正）" };
inline const juce::String themeToggle { u8"ライト/ダーク" };
inline const juce::String emergencyStopHint { u8"Esc: 緊急停止" };
inline const juce::String shortcutHint { u8"Space: 測定  N: 発振器  D: 遅延検出  S: スナップ  R: リセット" };

// ---- スナップショット（仕様 3.5 / 5.2 / 6.2）----
inline const juce::String snapshotDialogTitle { u8"スナップショットを保存" };
inline const juce::String snapshotNameLabel { u8"名前" };
inline const juce::String snapshotSystemLabel { u8"系統" };
inline const juce::String snapshotSaveButton { u8"保存" };
inline const juce::String snapshotCancelButton { u8"キャンセル" };
inline const juce::String snapshotDeleteButton { u8"削除" };
inline const juce::String snapshotDiffButton { u8"差分 (A-B)" };
inline const juce::String snapshotSelectA { u8"A" };
inline const juce::String snapshotSelectB { u8"B" };
inline const juce::String snapshotExportCsv { u8"CSV書き出し" };
inline const juce::String snapshotExportRew { u8"REW形式で書き出し" };
inline const juce::String snapshotExportTitle { u8"測定データの書き出し" };
inline const juce::String noSnapshots { u8"スナップショットはまだありません" };

// ---- IR・遅延（仕様 3.3）----
inline const juce::String delayLabel { u8"遅延" };
inline const juce::String delayMsUnit { u8"ms" };
inline const juce::String delaySamplesUnit { u8"サンプル" };
inline const juce::String delayMetresUnit { u8"m" };
inline const juce::String delayFeetUnit { u8"ft" };
inline const juce::String delayNotDetected { u8"遅延を検出できません" };
inline const juce::String delayApplyButton { u8"遅延を適用" };
inline const juce::String delayApplyManualButton { u8"手動値を適用" };
inline const juce::String delayClearButton { u8"補正を解除" };
inline const juce::String delayRawPhaseToggle { u8"生位相を表示" };
inline const juce::String delayPhatToggle { u8"GCC-PHAT" };
inline const juce::String delayTemperatureLabel { u8"気温" };
inline const juce::String delayDistanceLabel { u8"距離" };
inline const juce::String delayProposalLabel { u8"提案" };
inline const juce::String delayDistanceCheckLabel { u8"スピーカーからマイクまで約" };
inline const juce::String delayDistanceCheckSuffix { u8"m。実際の距離と合っているか確認してください" };
inline const juce::String delayDistanceMismatch { u8"入力した距離と10%以上ずれています。ピーク誤検出・反射・機器内の処理遅延の可能性があります" };
inline const juce::String irLabel { u8"インパルス応答" };
inline const juce::String irPeakAlignToggle { u8"ピークを0 msに揃える" };
inline const juce::String irEtcToggle { u8"ETC（対数エンベロープ）" };
inline const juce::String systemDelayLabel { u8"システム遅延（ループバック測定値）" };
inline const juce::String spatialDelayLabel { u8"空間伝播時間" };
inline const juce::String loopbackSection { u8"システム補正（ループバック校正）" };
inline const juce::String loopbackMeasureButton { u8"システム補正を測定" };
inline const juce::String loopbackMeasuring { u8"測定中…（平均が16回以上たまったら再度押す）" };
inline const juce::String loopbackClearButton { u8"補正を削除" };
inline const juce::String loopbackEnableToggle { u8"補正を適用" };
inline const juce::String loopbackStaleWarning { u8"接続・サンプルレート・ゲインが変わったら再測定してください" };
inline const juce::String loopbackInstruction { u8"出力を入力へケーブルで戻し、ピンクノイズを出して測定してください" };

// ---- 系統（仕様 3.3.4）----
inline const juce::String systemsSection { u8"系統" };
inline const juce::String systemsAll { u8"全体（合成）" };
inline const juce::String systemsAddButton { u8"系統を追加" };
inline const juce::String systemsRemoveButton { u8"削除" };
inline const juce::String systemsNameLabel { u8"名前" };
inline const juce::String systemsChannelLabel { u8"出力ch" };
inline const juce::String systemsPlayButton { u8"鳴らす" };
inline const juce::String systemsMuteOthersHint { u8"測定の開始前に、他の系統をミュートしてください" };
inline const juce::String compareSection { u8"系統比較（ディレイタワー / サブの位相合わせ / ステージ）" };
inline const juce::String compareSelectA { u8"基準（メイン）" };
inline const juce::String compareSelectB { u8"比較（サブ/ディレイ/モニター）" };
inline const juce::String compareArrivalDifference { u8"到達時間差" };
inline const juce::String compareSuggestedDelay { u8"提案する遅延" };
inline const juce::String comparePolarity { u8"極性" };
inline const juce::String comparePolarityKeep { u8"そのまま" };
inline const juce::String comparePolarityInvert { u8"反転を推奨" };
inline const juce::String compareCrossover { u8"クロスオーバー候補" };
inline const juce::String compareFineAdjust { u8"微調整" };
inline const juce::String compareTowerResult { u8"ディレイタワー：" };
inline const juce::String compareApplyDelaySuffix { u8"delay をかけてください" };
inline const juce::String compareMicPointNote { u8"※ このマイク位置での値です" };
inline const juce::String compareSumPrediction { u8"合成予測" };
inline const juce::String compareNoSnapshots { u8"比較するスナップショットが2つ必要です" };

// ---- RTA・SPL・校正（仕様 3.4 / 3.5.1 / 3.6）----
inline const juce::String rtaSection { u8"RTA" };
inline const juce::String rtaBandsLabel { u8"バンド" };
inline const juce::String rtaPeakHoldButton { u8"ピークホールド解除" };
inline const juce::String rtaNoSignal { u8"入力信号がありません（マイク・デバイス設定を確認）" };
inline const juce::String splResetButton { u8"SPLリセット" };
inline const juce::String rtaAvgTimeLabel { u8"平均時間" };
inline const juce::String splSection { u8"SPL" };
inline const juce::String splWeightingLabel { u8"重み付け" };
inline const juce::String splTimeConstantLabel { u8"時定数" };
inline const juce::String splFast { u8"Fast" };
inline const juce::String splSlow { u8"Slow" };
inline const juce::String splMedium { u8"0.5 秒（見やすい）" };
inline const juce::String splLeqLabel { u8"Leq" };
inline const juce::String splLeq1min { u8"1分" };
inline const juce::String splLeq15min { u8"15分" };
inline const juce::String splLeqSession { u8"開始から" };
inline const juce::String splPeakLabel { u8"ピーク" };
inline const juce::String splUncalibrated { u8"※ 校正しない状態のSPLは相対値です" };
inline const juce::String calibrationSection { u8"SPL校正" };
inline const juce::String calibrationOffsetLabel { u8"オフセット" };
inline const juce::String calibrationNoiseMeterLabel { u8"騒音計の値 (dB)" };
inline const juce::String calibrationApplyButton { u8"オフセットを保存" };
inline const juce::String calibrationClearButton { u8"校正をリセット" };
inline const juce::String calibrationComparatorGuide { u8"手持ちの騒音計と比較する方法: 1. アプリの校正信号または安定した外部音源を鳴らす 2. マイクと騒音計を同じ位置に置く 3. 騒音計の値（dB(A) Fast）を入力する" };
inline const juce::String calibrationCalibratorGuide { u8"校正器を使う方法: 校正器（94/114 dB）をマイクに装着し、その値を入力する" };
inline const juce::String calibrationSignalLabel { u8"校正信号" };
inline const juce::String calibrationSignalBandNoise { u8"1kHz帯域ピンクノイズ" };
inline const juce::String calibrationSignalTone { u8"1kHzトーン" };
inline const juce::String calibrationSignalPink { u8"広帯域ピンクノイズ" };
inline const juce::String calibrationAccuracyNote { u8"手持ちの騒音計の精度は ±1.5 dB（94 dB・1 kHz）です。校正後もこの誤差が絶対値に残ります" };
inline const juce::String gainAssistSection { u8"ゲイン設定アシスト" };
inline const juce::String gainAssistTargetLabel { u8"目標SPL" };
inline const juce::String gainAssistRaise { u8"ゲインを上げてください（あと約" };
inline const juce::String gainAssistLower { u8"ゲインを下げてください" };
inline const juce::String gainAssistOk { u8"OK：最適な範囲です" };
inline const juce::String gainAssistTooHot { u8"クリップの恐れがあります" };
inline const juce::String gainAssistPeak { u8"ピーク" };
inline const juce::String gainAssistRms { u8"RMS" };
inline const juce::String gainAssistHeadroom { u8"ピーク余裕" };
inline const juce::String gainAssistToCalibration { u8"このままSPL校正へ進む" };
inline const juce::String gainAssistRefNote { u8"Ref入力も同じ手順でレベル確認ができます" };
inline const juce::String gainAssistRecalibrate { u8"ゲインを変えたときはSPLの再校正を促します" };

// ---- S/N（仕様 3.5.1）----
inline const juce::String snrSection { u8"S/N" };
inline const juce::String snrNoiseFloorButton { u8"ノイズフロアを測る" };
inline const juce::String snrNoiseFloorStop { u8"ノイズフロア測定を止める" };
inline const juce::String snrNoiseFloorHint { u8"信号を出さずに暗騒音を測って保存します" };
inline const juce::String snrOverall { u8"全体 S/N" };
inline const juce::String snrGood { u8"良好" };
inline const juce::String snrFair { u8"要注意" };
inline const juce::String snrPoor { u8"不良" };
inline const juce::String snrHintNoise { u8"S/N が低い帯域: ノイズが原因の可能性" };
inline const juce::String snrHintReflection { u8"S/N は高いのにコヒーレンスが低い帯域: 反射・残響が原因の可能性" };

// ---- EQ案（仕様 4章）----
inline const juce::String eqSection { u8"補正EQ案" };
inline const juce::String eqCreateButton { u8"補正EQ案を作成" };
inline const juce::String eqTargetLabel { u8"ターゲット" };
inline const juce::String eqTargetFlat { u8"フラット" };
inline const juce::String eqTargetTilt { u8"高域傾斜" };
inline const juce::String eqTargetTiltLabel { u8"傾斜 (dB/oct)" };
inline const juce::String eqTargetCsv { u8"CSV読込" };
inline const juce::String eqRangeLabel { u8"補正範囲" };
inline const juce::String eqCoherenceGateLabel { u8"コヒーレンスしきい値" };
inline const juce::String eqMaxBandsLabel { u8"最大帯域数" };
inline const juce::String eqValleyBoostToggle { u8"谷へのブーストを許す" };
inline const juce::String eqTypeLabel { u8"種別" };
inline const juce::String eqFreqLabel { u8"周波数" };
inline const juce::String eqGainLabel { u8"ゲイン" };
inline const juce::String eqQLabel { u8"Q" };
inline const juce::String eqPredictionLegend { u8"予測（補正後）" };
inline const juce::String eqExportCsv { u8"CSV書き出し" };
inline const juce::String eqExportText { u8"テキスト書き出し" };
inline const juce::String eqNoProposal { u8"測定後に「補正EQ案を作成」を押してください" };
inline const juce::String eqConverged { u8"収束しました" };
inline const juce::String eqNotConverged { u8"未収束（帯域数の上限か制約に達しました）" };
inline const juce::String eqBell { u8"Bell" };
inline const juce::String eqLowShelf { u8"ローシェルフ" };
inline const juce::String eqHighShelf { u8"ハイシェルフ" };
inline const juce::String eqLowPass { u8"LPF" };
inline const juce::String eqHighPass { u8"HPF" };

// ---- 上限測定（仕様 3.7）----
inline const juce::String limitSection { u8"ヘッドルーム・歪み測定" };
inline const juce::String limitSafetySection { u8"安全設定" };
inline const juce::String limitCeilingLabel { u8"出力上限 (dBFS)" };
inline const juce::String limitStepLabel { u8"ステップ (dB)" };
inline const juce::String limitMaxDuration { u8"測定時間の上限 (秒)" };
inline const juce::String limitWarnMessage { u8"⚠ 大音響でスピーカーを壊す可能性があります。開始前に聴覚を保護してください" };
inline const juce::String limitAcknowledge { u8"警告を確認しました" };
inline const juce::String limitStartButton { u8"レベルステップ測定を開始" };
inline const juce::String limitStopButton { u8"停止" };
inline const juce::String limitCompressionLabel { u8"圧縮量" };
inline const juce::String limitThdButton { u8"THD測定（スイープ）" };
inline const juce::String limitThdLevel { u8"レベル" };
inline const juce::String limitThdValue { u8"THD" };
inline const juce::String limitThermalButton { u8"熱圧縮測定（30秒）" };
inline const juce::String limitThermalDrop { u8"SPL低下" };
inline const juce::String limitSuggestion { u8"リミッターしきい値の目安" };
inline const juce::String limitAmpGainLabel { u8"アンプゲイン (dB)" };
inline const juce::String limitSpeakerRatingLabel { u8"スピーカー最大入力 (W) / インピーダンス (Ω)" };
inline const juce::String limitAutoStopped { u8"圧縮・歪みがしきい値を超えたため自動停止しました" };
inline const juce::String limitStepProgress { u8"ステップ" };

// ---- 本番モード（仕様 5.4）----
inline const juce::String liveExitButton { u8"戻る（長押し）" };
inline const juce::String liveThresholdLabel { u8"しきい値" };
inline const juce::String liveThresholdGreen { u8"緑" };
inline const juce::String liveThresholdYellow { u8"黄" };
inline const juce::String liveLogStarted { u8"SPLログの記録を開始しました" };
inline const juce::String liveLogStopped { u8"SPLログの記録を終了しました" };
inline const juce::String liveWaterfallToggle { u8"ウォーターフォール" };
inline const juce::String liveSplHistory { u8"直近60分の推移" };
inline const juce::String liveLongPressHint { u8"操作は長押しで（誤操作防止）" };
inline const juce::String waterfallTimeLabel { u8"表示時間" };
inline const juce::String waterfallResolutionLabel { u8"分解能" };
inline const juce::String waterfallRangeLabel { u8"レベル範囲" };

// ---- 温度（仕様 3.8）----
inline const juce::String temperatureLabel { u8"気温" };
inline const juce::String temperatureManualNote { u8"USB温度計は未対応（機種未決定）。手入力で運用します" };
inline const juce::String temperatureLogSection { u8"温度ログ" };
inline const juce::String temperatureDriftWarning { u8"気温の変化で遅延が" };
inline const juce::String temperatureDriftSuffix { u8"ms ずれました。ディレイタワーの設定を見直してください" };

// ---- ツール（仕様 5.6）----
inline const juce::String toolDistanceDelay { u8"距離⇄遅延" };
inline const juce::String toolSpeedOfSound { u8"音速" };
inline const juce::String toolWavelength { u8"周波数⇄波長" };
inline const juce::String toolCombFilter { u8"コムフィルタ" };
inline const juce::String toolDistanceAttenuation { u8"距離減衰" };
inline const juce::String toolLevelConversion { u8"レベル換算" };
inline const juce::String toolTempo { u8"テンポ⇄時間" };
inline const juce::String toolQBandwidth { u8"Q⇄帯域幅" };
inline const juce::String toolMetres { u8"m" };
inline const juce::String toolFeet { u8"ft" };
inline const juce::String toolHz { u8"Hz" };
inline const juce::String toolHalfWave { u8"半波長" };
inline const juce::String toolQuarterWave { u8"1/4波長" };
inline const juce::String toolSourcesLabel { u8"音源数" };
inline const juce::String toolOctaves { u8"oct" };
inline const juce::String toolBpm { u8"BPM" };

// ---- 会場・プロジェクト（仕様 6.2 / 6.4）----
inline const juce::String venueSection { u8"会場・測定メモ" };
inline const juce::String venueNameLabel { u8"会場名" };
inline const juce::String venueDateLabel { u8"日付" };
inline const juce::String venueTempLabel { u8"気温 (℃)" };
inline const juce::String venueHumidityLabel { u8"湿度 (%)" };
inline const juce::String venueNotesLabel { u8"メモ" };
inline const juce::String venueEquipmentLabel { u8"使用機材・マイクゲイン位置" };
inline const juce::String venueMicPoints { u8"マイクポイント" };
inline const juce::String venueMicPointName { u8"名前" };
inline const juce::String venueMicPointDesc { u8"位置の説明" };
inline const juce::String venueSessions { u8"測定セッション" };
inline const juce::String venueSaveProject { u8"プロジェクト保存 (.eea)" };
inline const juce::String venueLoadProject { u8"プロジェクトを開く" };
inline const juce::String venueNewButton { u8"新規会場" };
inline const juce::String venueSearchLabel { u8"検索" };
inline const juce::String venueReportButton { u8"レポート書き出し (PNG)" };
inline const juce::String venueSaveOk { u8"保存しました" };
inline const juce::String venueSaveFailed { u8"保存に失敗しました" };
inline const juce::String venueLoadOk { u8"読み込みました" };
inline const juce::String venueLoadFailed { u8"読み込みに失敗しました" };
inline const juce::String venueSnapshotMemo { u8"スナップショットのメモ" };

// ---- 書き出し ----
inline const juce::String exportIrWav { u8"IR を WAV で書き出し" };
inline const juce::String exportGraphPng { u8"グラフをPNG保存" };
inline const juce::String exportSplLog { u8"SPLログをCSV保存" };

// ---- ウィザード（仕様 5.5）----
inline const juce::String wizardTitle { u8"はじめの設定" };
inline const juce::String wizardStep1 { u8"1. オーディオデバイスとチャンネルを選んでください（上部のバー）" };
inline const juce::String wizardStep2 { u8"2. ループバック校正: 出力を入力へケーブルで戻して「システム補正を測定」" };
inline const juce::String wizardStep3 { u8"3. ゲイン調整: ピンクノイズを出して入力レベルを最適化します" };
inline const juce::String wizardStep4 { u8"4. 準備完了。測定を開始できます" };
inline const juce::String wizardSkip { u8"スキップ" };
inline const juce::String wizardNext { u8"次へ" };
inline const juce::String wizardClose { u8"閉じる" };

// ---- 汎用 ----
inline const juce::String secondsUnit { u8"秒" };
inline const juce::String hzUnit { u8"Hz" };
inline const juce::String kHzUnit { u8"kHz" };
inline const juce::String dbUnit { u8"dB" };
inline const juce::String dbfsUnit { u8"dBFS" };
inline const juce::String dbsplUnit { u8"dB SPL" };
inline const juce::String msUnit { u8"ms" };
inline const juce::String samplesUnit { u8"samples" };
inline const juce::String addButton { u8"追加" };
inline const juce::String removeButton { u8"削除" };
inline const juce::String renameButton { u8"名前変更" };
inline const juce::String fileChooserOpen { u8"開く" };
inline const juce::String wavFileChooserTitle { u8"WAVファイルを選択" };
inline const juce::String wavLoadFailed { u8"WAVファイルを読み込めませんでした" };
inline const juce::String cancelButton { u8"キャンセル" };

// ---- Issue #141: オシレータ/ショートカット表示 ----
inline const juce::String oscillatorActiveLabel { u8"出力中" };
inline const juce::String shortcutsHint { u8"Space 測定 / N 発振器 / D 遅延 / S スナップ / R リセット / Esc 全停止" };

// ---- Issue #142: Delay Finder 改善 ----
inline const juce::String delayAutoApplyToggle { u8"検出したら適用" };
inline const juce::String delayTrackFollowToggle { u8"Track（追従）" };
inline const juce::String irClipHoldToggle { u8"IR保持" };

// ---- Issue #143: スナップショット管理 ----
inline const juce::String snapshotColorLabel { u8"色" };
inline const juce::String snapshotEditNameButton { u8"名前変更" };
inline const juce::String snapshotDbOffsetLabel { u8"dBオフセット" };
inline const juce::String snapshotDelayOffsetLabel { u8"遅延オフセット" };
inline const juce::String snapshotSmoothingLabel { u8"平滑化" };
inline const juce::String snapshotMoveUpButton { u8"上へ" };
inline const juce::String snapshotMoveDownButton { u8"下へ" };
inline const juce::String snapshotSelectGlobal { u8"グローバル設定" };

// ---- Issue #144: 3段表示・縦スケールプリセット ----
inline const juce::String viewModeLabel { u8"表示モード" };
inline const juce::String viewModeAmpPhaseCoh { u8"振幅/位相/コヒーレンス" };
inline const juce::String viewModeAmpPhase { u8"振幅/位相" };
inline const juce::String viewModeAmpOnly { u8"振幅のみ" };
inline const juce::String scalePresetLabel { u8"縦スケール" };
inline const juce::String scalePresetAuto { u8"自動" };
inline const juce::String scalePreset6 { u8"±6 dB" };
inline const juce::String scalePreset12 { u8"±12 dB" };
inline const juce::String scalePreset24 { u8"±24 dB" };
inline const juce::String scalePreset48 { u8"±48 dB" };
inline const juce::String scaleLockButton { u8"スケール固定" };

// ---- Issue #145: カーソル・マーカー ----
inline const juce::String cursorLabel { u8"カーソル" };
inline const juce::String markerLabel { u8"マーカー" };
inline const juce::String markerAddButton { u8"マーカー追加" };
inline const juce::String markerClearButton { u8"マーカー全消去" };
inline const juce::String markerDeltaLabel { u8"差分" };
inline const juce::String peakSearchNext { u8"次のピーク" };
inline const juce::String peakSearchPrev { u8"前のピーク" };
inline const juce::String peakSearchHighest { u8"最大ピーク" };

// ---- Issue #146: 入力メーター改善 ----
inline const juce::String inputMeterPeakLabel { u8"ピーク" };
inline const juce::String inputMeterRmsLabel { u8"RMS" };
inline const juce::String inputMeterClipLabel { u8"CLIP" };
inline const juce::String inputMeterClearClipButton { u8"クリップ解除" };
inline const juce::String tfClipWarning { u8"⚠ 入力クリップ検知" };

// ---- Issue #147: 平滑化プリセット・FIFO ----
inline const juce::String smoothingPresetLabel { u8"平滑化プリセット" };
inline const juce::String averagingModeFifo { u8"FIFO（直近N回の等重み）" };

// ---- Issue #148: 確認ダイアログ・Undo ----
inline const juce::String confirmDeleteSnapshot { u8"このスナップショットを削除しますか？" };
inline const juce::String confirmDeleteSystem { u8"この系統を削除しますか？" };
inline const juce::String confirmClearLoopback { u8"ループバック校正を削除しますか？" };
inline const juce::String confirmClearProject { u8"プロジェクト全体をクリアしますか？元に戻せません。" };
inline const juce::String undoButton { u8"元に戻す" };
inline const juce::String redoButton { u8"やり直し" };

// ---- Issue #149: EQタブグラフ・ツールチップ ----
inline const juce::String eqGraphTitle { u8"周波数特性" };
inline const juce::String eqGraphMeasured { u8"実測" };
inline const juce::String eqGraphTarget { u8"ターゲット" };
inline const juce::String eqGraphPrediction { u8"予測" };
inline const juce::String eqTooltipTarget { u8"補正目標の周波数特性（フラット/傾斜/CSV）" };
inline const juce::String eqTooltipRange { u8"EQ補正を適用する周波数範囲" };
inline const juce::String eqTooltipCoherence { u8"このコヒーレンス未満の帯域は補正対象から除外" };
inline const juce::String eqTooltipBands { u8"提案するPEQ帯域の最大数" };
inline const juce::String eqTooltipValley { u8"谷（ディップ）へのブーストを許可するか" };
inline const juce::String eqTooltipCreate { u8"現在の測定に対して補正EQ案を計算" };

// ---- グラフのマーカー・カーソル（issue #145 / #146）----
inline const juce::String markerAddHere { u8"ここにマーカーを置く（Shift+クリック）" };
inline const juce::String markerAddPeak { u8"表示範囲のピークにマーカー" };
inline const juce::String markerRemoveNear { u8"近くのマーカーを消す" };
inline const juce::String markerClearAll { u8"マーカーをすべて消す" };
inline const juce::String graphResetView { u8"表示範囲をリセット（ダブルクリック）" };
inline const juce::String cursorDeltaLive { u8"LIVE差" };
inline const juce::String graphClipWarning { u8"入力がクリップしています（平均は信頼できません）" };


// ---- 伝達関数タブのツールバー・トレース管理（issue #143 / #144 / #145 / #148）----
inline const juce::String viewModeThree { u8"振幅/位相/γ²（3段）" };
inline const juce::String viewModeTwo { u8"振幅/位相（2段）" };
inline const juce::String viewModeTooltip { u8"グラフの段構成" };
inline const juce::String scaleAuto { u8"縦軸: 自動" };
inline const juce::String scaleTooltip { u8"振幅の縦軸レンジ。固定すると目盛りが揺れない（dB帯でホイール・縦ドラッグでも調整）" };
inline const juce::String coherenceBlankingToggle { u8"低γ²を非表示" };
inline const juce::String coherenceBlankingTooltip { u8"オン: コヒーレンスがしきい値未満の区間を消す／オフ: 薄く描く" };
inline const juce::String coherenceThresholdTooltip { u8"コヒーレンスのしきい値" };
inline const juce::String markerPeakButton { u8"ピークにマーカー" };
inline const juce::String markerTooltip { u8"Shift+クリックで任意の周波数にマーカー、右クリックでメニュー" };
inline const juce::String diffNone { u8"（差分なし）" };
inline const juce::String undoDeleteButton { u8"元に戻す" };
inline const juce::String snapshotToggleVisible { u8"表示切替" };
inline const juce::String snapshotShow { u8"表示する" };
inline const juce::String snapshotHide { u8"隠す" };
inline const juce::String snapshotHiddenSuffix { u8"（非表示）" };
inline const juce::String snapshotShowPhase { u8"位相も表示" };
inline const juce::String snapshotDelayOffsetMsLabel { u8"位相の遅延 (ms)" };
inline const juce::String snapshotEditTitle { u8"スナップショットを編集" };
inline const juce::String snapshotAverageButton { u8"平均を作成" };
inline const juce::String snapshotAverageTooltip { u8"表示中のスナップショットを平均したトレースを作る（複数マイク位置の空間平均）" };
inline const juce::String snapshotAverageNeedTwo { u8"平均するには、表示中のスナップショットが2つ以上必要です。" };
inline const juce::String snapshotAverageMismatch { u8"FFTサイズかサンプルレートが異なるスナップショットは平均できません。" };
inline const juce::String snapshotAverageName { u8"平均" };
inline const juce::String snapshotAverageMicPoint { u8"平均" };
inline const juce::String snapshotAverageNote { u8"平均したスナップショット:" };


// ---- Delay Finder（issue #142）----
inline const juce::String delayMeasureTooltip { u8"現在の入力から遅延を検出する（D キーで検出→即適用）" };
inline const juce::String delayAutoApplyTooltip { u8"検出に成功したら、そのまま遅延を適用する" };
inline const juce::String delayTrackTooltip { u8"測定中、約2秒ごとに遅延を再検出して追従する（伝達関数タブを見ながら使える）" };
inline const juce::String irHoldTooltip { u8"測定を止めても最後の IR を表示し続ける。IR グラフをクリックするとその時刻を遅延として適用" };
inline const juce::String delayTrackWaiting { u8"Track: 測定開始待ち" };
inline const juce::String delayTrackLost { u8"Track: 検出できません（信号を確認）" };
inline const juce::String delayTrackFollowed { u8"Track: 追従 " };
inline const juce::String delayTrackStable { u8"Track: 安定 " };
inline const juce::String delayAutoApplied { u8"遅延を自動で設定: " };

inline const juce::String meterTooltip { u8"数値はピーク (dBFS)。クリップは保持され、クリック（または R）で解除" };

inline const juce::String liveWarnThreshold { u8"注意" };
inline const juce::String liveLimitThreshold { u8"上限" };

inline const juce::String liveLogInactive { u8"CSVログ停止中（保存先を確認）" };

inline const juce::String confirmClearCalibration { u8"SPL校正値を消去しますか？（騒音計と比べて測り直すまで、SPLは相対値になります）" };
inline const juce::String confirmDeleteMicPoint { u8"このマイクポイントを削除しますか？" };
inline const juce::String confirmLoadProject { u8"プロジェクトを読み込むと、現在の会場・スナップショットは置き換わります。必要なら先に保存してください。続けますか？" };
inline const juce::String clearProjectButton { u8"プロジェクトをクリア" };
inline const juce::String systemsSetTarget { u8"測定対象にする" };
inline const juce::String systemsSetTargetTooltip { u8"選択中の系統だけから励起信号を出す（行のダブルクリックでも可）" };

inline const juce::String eqLevelAlignedPrefix { u8"ターゲットのレベル合わせ: " };
inline const juce::String eqLevelAlignTooltip { u8"測定の絶対レベルは機器のゲインで決まるため、補正範囲内の中央値にターゲットを合わせてから EQ 案を作る" };

inline const juce::String limitCeilingInvalid { u8"出力上限は −60〜0 dBFS の数値で入力してください" };

inline const juce::String uiWatchdogStoppedNotice { u8"UI停止を検知：励起音を自動停止しました" };
// 入力が完全な無音（全サンプル 0）のときの警告
inline const juce::String inputDigitalSilenceNotice { u8"入力が完全に無音です（デバイス選択・マイク許可・ケーブルを確認してください）" };
inline const juce::String builtInMicSilenceNotice { u8"内蔵マイクが無音です（macOS では内蔵スピーカーの音量が 0／ミュートだと内蔵マイクも無音になります）" };
inline const juce::String deviceRestartStoppedNotice { u8"オーディオデバイスが停止したため励起音を止めました（自動では再開しません）" };

inline const juce::String loopbackRestoreFailed { u8"保存されていたループバック校正を読み込めませんでした。補正なしで測定しています。IR・遅延タブで測り直してください。" };

inline const juce::String eqBandFreqTooltip { u8"中心周波数 (Hz)。10 Hz〜fs×0.45" };
inline const juce::String eqBandGainTooltip { u8"ゲイン (dB)。−24〜+24 dB" };
inline const juce::String eqBandQTooltip { u8"Q（帯域の鋭さ）。0.1〜30" };

inline const juce::String wizardFootnote { u8"※ 各ステップは「IR・遅延」タブ（ループバック校正）と「RTA・SPL」タブ（校正・ゲイン調整）から実行できます。Esc でいつでも出力を止められます。" };
inline const juce::String calibrationNoSignal { u8"校正用の音を入力で検出できません。音源・入力ゲイン・接続を確認してください。" };
inline const juce::String calibrationMeterRange { u8"騒音計の値は 30〜140 dB で入力してください" };
inline const juce::String calibrationAcquiring { u8"A/Fast の新しい測定値を取得しています…" };
inline const juce::String rtaScaleUncalibrated { u8"dBFS（未校正）" };
inline const juce::String rtaScaleCalibrated { u8"dB SPL（校正済み）" };
inline const juce::String averagesPrefix { u8"平均: " };
inline const juce::String averagesSuffix { u8" 回" };


// ---- 2026-11 リデザイン: 作業ナビ ----
inline const juce::String navFlowHeader { u8"作業の流れ" };
inline const juce::String navToolsHeader { u8"道具" };
inline const juce::String navShortcutsButton { u8"キー一覧" };
inline const juce::String navSetup { u8"準備" };
inline const juce::String navSetupSub { u8"機器・レベル・補正" };
inline const juce::String navMeasure { u8"測定" };
inline const juce::String navMeasureSub { u8"伝達関数を見る" };
inline const juce::String navDelay { u8"遅延・IR" };
inline const juce::String navDelaySub { u8"インパルス応答・系統合わせ" };
inline const juce::String navEq { u8"EQ" };
inline const juce::String navEqSub { u8"補正案を作る" };
inline const juce::String navApply { u8"入れる値" };
inline const juce::String navApplySub { u8"卓・プロセッサーへ" };
inline const juce::String navRta { u8"RTA・SPL" };
inline const juce::String navLimit { u8"上限測定" };
inline const juce::String navTools { u8"計算ツール" };
inline const juce::String navVenue { u8"会場・記録" };
inline const juce::String shortcutHelpTitle { u8"キー操作" };
inline const juce::String shortcutHelpBody { u8"Space   測定の開始 / 停止\nN   発振器（ピンクノイズ）の ON / OFF\nD   遅延を検出して適用\nS   スナップショット保存\nR   平均リセット・クリップ解除\nEsc   全停止（どの画面でも）\n1〜5   作業の流れの画面へ移動" };

// ---- ステータス帯 ----
inline const juce::String statusIo { u8"入出力" };
inline const juce::String statusClock { u8"クロック" };
inline const juce::String statusClockSame { u8"同一デバイス" };
inline const juce::String statusClockCompensating { u8"補正中 " };
inline const juce::String statusClockSeparate { u8"別デバイス" };
inline const juce::String statusOutput { u8"出力" };
inline const juce::String statusDelayNone { u8"未設定" };
inline const juce::String statusAverages { u8"平均" };
inline const juce::String statusStopped { u8"停止中" };
inline const juce::String statusCorrection { u8"ループバック補正" };
inline const juce::String statusOn { u8"ON" };
inline const juce::String statusNone { u8"なし" };
inline const juce::String statusCalibration { u8"SPL校正" };
inline const juce::String statusDone { u8"済" };
inline const juce::String statusNotYet { u8"未" };
inline const juce::String statusInput { u8"入力" };
inline const juce::String statusNoVenue { u8"会場未設定" };
inline const juce::String statusSystemPrefix { u8"系統: " };
inline const juce::String liveModeTooltip { u8"本番中の SPL 監視画面に切り替えます" };

// ---- 入れる値 ----
inline const juce::String applySheetTitle { u8"システムに入れる値" };
inline const juce::String applySheetLead { u8"測って決めた値を系統ごとにまとめます。卓・プロセッサーに入れたらチェックを付けてください。" };
inline const juce::String applySummaryTitle { u8"入れる値" };
inline const juce::String applyDelay { u8"ディレイ" };
inline const juce::String applyUndecided { u8"未決定" };
inline const juce::String applyPolarityNormal { u8"正相" };
inline const juce::String applyPolarityInvert { u8"反転" };
inline const juce::String applyEqFlat { u8"なし（フラット）" };
inline const juce::String applyEqBandsSuffix { u8" バンド" };
inline const juce::String applyEqUndecidedHint { u8"「4 EQ」で補正案を作って記録します" };
inline const juce::String applyNote { u8"メモ" };
inline const juce::String applyNotePlaceholder { u8"根拠など（例: サブ基準、FOH で測定）" };
inline const juce::String applyEnteredShort { u8"卓" };
inline const juce::String applyEnteredToggle { u8"卓に入力済み" };
inline const juce::String applyEnteredDone { u8"✓ 卓に入力済み" };
inline const juce::String applyEnteredNot { u8"未入力" };
inline const juce::String applyOpenSheet { u8"入れる値を開く" };
inline const juce::String applyRecordEq { u8"EQ 案を記録" };
inline const juce::String applyRecordEqTooltip { u8"いまの補正EQ案を、この系統の入れる値として記録します" };
inline const juce::String applyRecordDelay { u8"入れる値に記録" };
inline const juce::String applyRecordDelayTooltip { u8"提案したディレイと極性を、系統の入れる値として記録します" };
inline const juce::String applyMeasureThis { u8"この系統を測る" };
inline const juce::String applyMeasureThisTooltip { u8"これからの測定・スナップショットをこの系統として扱います" };
inline const juce::String applyMeasuringNow { u8"測定中の系統" };
inline const juce::String applyAddSystem { u8"系統を追加" };
inline const juce::String applyDefaultSystemName { u8"系統" };
inline const juce::String applyCopy { u8"テキストをコピー" };
inline const juce::String applyExport { u8"書き出し (.txt)" };
inline const juce::String applyEmpty { u8"系統がまだありません。「系統を追加」で メイン / サブ / ディレイ などを作ると、\nここに入れる値が並びます。" };
inline const juce::String applyRecorded { u8"入れる値に記録しました: " };

// ---- 準備 ----
inline const juce::String setupTitle { u8"準備" };
inline const juce::String setupLead { u8"機器とチャンネルを選び、下の確認リストが OK になったら測定へ進みます。" };
inline const juce::String setupDevicesTitle { u8"オーディオ機器" };
inline const juce::String setupChannelsTitle { u8"チャンネル" };
inline const juce::String setupLevelsTitle { u8"レベル" };
inline const juce::String setupChecklistTitle { u8"測る前の確認" };
inline const juce::String setupGoMeasure { u8"測定へ進む  →" };
inline const juce::String checkMarkWarn { u8"注意" };
inline const juce::String checkMarkBad { u8"要対応" };
inline const juce::String checkMarkOptional { u8"任意" };
inline const juce::String checkMarkTodo { u8"確認" };
inline const juce::String checkDeviceTitle { u8"オーディオ機器" };
inline const juce::String checkDeviceMissing { u8"機器が開けていません。上で入力・出力を選んでください" };
inline const juce::String checkInputTitle { u8"マイクの入力レベル" };
inline const juce::String checkInputClip { u8"クリップしています。インターフェースのゲインを下げてください" };
inline const juce::String checkInputPlayNoise { u8"N でピンクノイズを出すと確認できます（目標ピーク −30〜−6 dBFS）" };
inline const juce::String checkInputHot { u8"大きすぎます（ゲインを下げる）: ピーク " };
inline const juce::String checkInputLow { u8"小さすぎます（ゲインを上げる）: ピーク " };
inline const juce::String checkInputOk { u8"ちょうどよい範囲です: ピーク " };
inline const juce::String checkOpenGainAssist { u8"ゲイン設定アシスト" };
inline const juce::String checkRefTitle { u8"Ref（基準信号）" };
inline const juce::String checkRefInternal { u8"内部参照（アプリが出している信号をそのまま使います）" };
inline const juce::String checkRefMissing { u8"Ref が選ばれていません" };
inline const juce::String checkRefExternal { u8"入力 ch " };
inline const juce::String checkRefSilent { u8"（信号が来ていません）" };
inline const juce::String checkClockTitle { u8"入出力のクロック" };
inline const juce::String checkClockSame { u8"同じ機器なのでずれません" };
inline const juce::String checkClockCompensating { u8"別の機器: ずれを自動で補正中 " };
inline const juce::String checkClockSeparate { u8"別の機器: 測定を始めるとずれを自動で補正します" };
inline const juce::String checkLoopbackTitle { u8"ループバック補正" };
inline const juce::String checkLoopbackOn { u8"適用中（インターフェースの特性を差し引いています）" };
inline const juce::String checkLoopbackOff { u8"なし。外部 Ref で測るなら不要、内部参照で精度を上げたいときに" };
inline const juce::String checkOpenLoopback { u8"ループバック補正" };
inline const juce::String checkSplTitle { u8"SPL 校正" };
inline const juce::String checkSplDone { u8"校正済み: オフセット " };
inline const juce::String checkSplNotYet { u8"未校正（SPL は相対値）。本番の音量管理に使うなら校正を" };
inline const juce::String checkOpenSpl { u8"SPL 校正" };


// ---- 操作バー ----
inline const juce::String transportMeasureOff { u8"測定開始   Space" };
inline const juce::String transportMeasureOn { u8"測定中   Space" };
inline const juce::String transportReset { u8"リセット   R" };
inline const juce::String transportNoiseOff { u8"発振   N" };
inline const juce::String transportNoiseOn { u8"発振中   N" };
inline const juce::String transportDelay { u8"遅延   D" };
inline const juce::String transportSave { u8"保存   S" };
inline const juce::String stopAllButton { u8"全停止   Esc" };
inline const juce::String navThemeButton { u8"テーマ" };

inline const juce::String graphAmpCaption { u8"振幅 dB" };
inline const juce::String graphPhaseCaption { u8"位相 °" };
inline const juce::String graphCohCaption { u8"コヒーレンス γ²" };

inline const juce::String applyRecordedShort { u8"✓ 記録しました" };

inline const juce::String liveRecording { u8"記録中" };
inline const juce::String liveMinutesSuffix { u8" 分経過" };
inline const juce::String liveElapsedSuffix { u8" 経過" };
inline const juce::String liveCurrentLevel { u8"現在の音圧" };

inline const juce::String tracesTitle { u8"トレース" };
inline const juce::String tracesHint { u8"S で今の測定を保存。チェックで表示、選ぶと右で調整できます" };

inline const juce::String delayBreakdownPrefix { u8"うち機器の遅延 " };
inline const juce::String delaySystemFromOs { u8"（OS の申告値）" };
inline const juce::String delaySystemFromLoopback { u8"（ループバック補正で測定）" };
inline const juce::String delayBreakdownRest { u8"、残り " };
inline const juce::String delayBreakdownNote { u8"。内部参照の遅延は機器の処理時間を含むため、距離そのものではありません（残りにもスピーカー内部の処理が入ります）" };

inline const juce::String calibrationAveraging { u8"3 秒間の平均を取っています（音を出したまま動かさないでください）… 残り " };

inline const juce::String checkMicTitle { u8"マイク補正" };
inline const juce::String checkMicNone { u8"なし（マイクの特性を補正しません）。校正ファイル（.txt / .frd）があれば読み込みます" };
inline const juce::String checkMicLoad { u8"ファイルを読み込む" };
inline const juce::String checkMicRemove { u8"補正を外す" };
inline const juce::String checkMicPoints { u8" 点、" };
inline const juce::String checkMicWithPhase { u8"、位相あり" };
inline const juce::String checkMicLoadFailed { u8"マイク補正ファイルを読めませんでした（周波数 と dB の列が必要です）" };
inline const juce::String statusMic { u8"マイク補正" };

inline const juce::String resolutionLabel { u8"分解能" };
inline const juce::String resolutionMtw { u8"MTW（帯域ごとに窓長）" };
inline const juce::String resolutionSingle { u8"単一 FFT" };
inline const juce::String resolutionTooltip { u8"MTW: 低域は長い窓で細かく、高域は短い窓で反射に汚されずに見る（遅延を合わせているときに有効）" };

inline const juce::String noticeRefSilent { u8"Ref が鳴っていません（発振 N か外部の信号を確認）" };
inline const juce::String noticeFewAverages { u8"平均がまだ少ない（数秒待つと落ち着きます）" };
inline const juce::String noticeNoDelay { u8"遅延が未設定です（D で検出）。位相が回り、コヒーレンスも下がります" };
inline const juce::String noticeNoDelayMtw { u8"遅延が未設定です（D で検出）。合わせると MTW（高域を短い窓で）に切り替わります" };
inline const juce::String noticeCorrectionStale { u8"ループバック補正を測ったときと FFT サイズ・レートが違うため、補正していません" };

inline const juce::String transportWavLoading { u8"読み込み中…" };

// ---- 残響・スイープ測定（2026-11）----
inline const juce::String navReverb { u8"残響" };
inline const juce::String reverbTitle { u8"残響時間（スイープ測定）" };
inline const juce::String reverbHint { u8"マイクを客席の代表位置に置き、会場を静かにしてから「スイープで測る」。"
                                        u8"鳴り終わって 2 秒待つと、帯域ごとの残響時間と減衰曲線が出ます。"
                                        u8"同じ結果が伝達関数とインパルス応答にも表示されます（下のバーで信号をスイープにして発振しても同じ）。" };
inline const juce::String reverbSweepLength { u8"スイープの長さ" };
inline const juce::String reverbMeasureButton { u8"スイープで測る" };
inline const juce::String reverbStopButton { u8"止める" };
inline const juce::String reverbIdle { u8"まだ測っていません" };
inline const juce::String reverbPlaying { u8"スイープ再生中… " };
inline const juce::String reverbTail { u8"残響を待っています…" };
inline const juce::String reverbAnalysing { u8"計算中…" };
inline const juce::String reverbFailed { u8"測れませんでした: " };
inline const juce::String reverbDone { u8"測定済み " };
inline const juce::String reverbTableTitle { u8"帯域ごとの残響時間（秒）" };
inline const juce::String reverbNoResult { u8"スイープで測ると、ここに残響時間が出ます" };
inline const juce::String reverbBand { u8"帯域 Hz" };
inline const juce::String reverbBroadband { u8"全帯域" };
inline const juce::String reverbDynamicRange { u8"S/N" };
inline const juce::String reverbTableNote { u8"棒は T20（−5〜−25 dB の傾きを 60 dB に伸ばした値）。T20 は S/N 35 dB 以上、T30 は 45 dB 以上のときだけ出します。"
                                             u8"S/N が足りない帯域は、出力を上げるか長いスイープで測り直してください。列をクリックすると右の曲線が切り替わります。" };
inline const juce::String reverbDecayTitle { u8"減衰曲線" };
inline const juce::String reverbDecayLegend { u8"太線: 減衰曲線（Schroeder）／ 細線: IR の包絡" };
inline const juce::String noticeSweepResult { u8"スイープの結果を表示中（鳴らし直すと更新。信号をノイズに戻すと通常の測定に戻ります）" };
inline const juce::String noticeSweepRunning { u8"スイープ測定中: 鳴り終わってから結果が出ます" };

inline const juce::String irPlaceholder { u8"インパルス応答はここに出ます。\n"
                                         u8"ピンクノイズを発振して「測定開始」（数秒で出ます）、または信号をスイープにして発振（鳴り終わると出ます）。\n"
                                         u8"ETC にすると反射と残響の減り方が dB で見えます。クリックした位置を遅延に使えます。" };

} // namespace eeq::strings
