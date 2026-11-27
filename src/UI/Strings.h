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
inline const juce::String themeToggle { u8"ライト/ダーク" };
inline const juce::String emergencyStopHint { u8"Esc: 緊急停止" };
inline const juce::String shortcutHint { u8"Space: 測定  S: スナップ  R: リセット" };

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
} // namespace eeq::strings
