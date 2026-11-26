#pragma once

#include "../DSP/TransferFunctionAccumulator.h"

#include "Core/JuceModules.h"

#include <vector>

namespace eeq::core
{

// スナップショット（仕様 3.5 / 5.2）: 測定結果に名前・色・系統・マイクポイントを付けたもの。
// データはマイクポイント単位で扱う前提（仕様 5.4.1）を守るため、micPoint を必ず持つ。
struct Snapshot
{
    juce::String name;
    juce::String systemName;  // 系統名（測定時に自動で付く。仕様 3.3.4）
    juce::String micPoint;    // マイクポイント名（FOH、ステージ上手など）
    juce::Colour colour { 0xffe0567a };
    juce::Time timestamp;
    double temperatureC = 20.0;
    juce::String note;        // スナップショットごとのメモ（仕様 6.4）
    double delaySamples = 0.0; // 測定時に検出していた遅延
    bool visible = true;       // グラフへの表示ON/OFF（仕様 5.2）
    bool isSelectedA = false;  // 差分（A−B）表示用の選択（仕様 5.2）
    bool isSelectedB = false;
    bool correctionApplied = false; // システム補正済みのフレームか（issue #32）

    eeq::dsp::SpectrumFrame frame;
};

struct MicPoint
{
    juce::String name;
    juce::String description;
};

// スピーカー構成の1系統（仕様 3.3.4 / 6.4）。会場メモと測定UIで共通にする
struct SpeakerSystemInfo
{
    juce::String name;
    int outputChannel = 0;
    juce::Colour colour { 0xff3fc9d2 };
};

struct Session
{
    juce::String name;
    juce::Time startedAt;
    std::vector<Snapshot> snapshots;
};

// 会場（仕様 6.4）。会場 → 測定セッション → スナップショットの階層で1つの .eea に保存する
struct Venue
{
    juce::String name;
    juce::Time date;
    double temperatureC = 20.0;
    double humidityPercent = 0.0;
    juce::String notes;
    juce::String equipment; // 使用機材とマイクのゲイン位置
    double subCrossoverHz = 0.0; // 0 = 未入力（仕様 3.3.3: 既定値を置かない）

    std::vector<SpeakerSystemInfo> systems;
    std::vector<MicPoint> micPoints;
    std::vector<Session> sessions;
};

// プロジェクト（.eea）の入出力。複数会場を保持でき、会場名・日付で一覧・検索する
class ProjectStore
{
public:
    ProjectStore();

    std::vector<Venue>& venues() noexcept { return venues_; }
    Venue& currentVenue() noexcept { return venues_[(size_t) juce::jlimit (0, (int) venues_.size() - 1, currentVenueIndex)]; }

    /** 現在のセッション（なければ現在の会場に新しく作る） */
    Session& currentSession();

    /** 現在のセッションのスナップショット一覧 */
    std::vector<Snapshot>& snapshots() { return currentSession().snapshots; }

    void setCurrentVenueIndex (int index) noexcept { currentVenueIndex = index; }
    int getCurrentVenueIndex() const noexcept { return currentVenueIndex; }

    /** 検索（会場名・日付。仕様 6.4） */
    std::vector<int> findVenues (const juce::String& query) const;

    /** .eea（JSON）として保存/読込。エラーメッセージ（成功時は空文字）を返す */
    [[nodiscard]] juce::String saveToFile (const juce::File& file) const;
    [[nodiscard]] juce::String loadFromFile (const juce::File& file);

    /** 起動時の復元先（前回保存したプロジェクトの自動復元用） */
    static juce::File defaultProjectFile();

    /** SpectrumFrame のJSONシリアライズ（SystemCorrection の保存など外部でも使う） */
    static juce::var spectrumFrameToVar (const eeq::dsp::SpectrumFrame& frame) { return frameToVar (frame); }
    /** SpectrumFrame のデシリアライズ。壊れたデータは fftSize=0 の空フレームを返す */
    static eeq::dsp::SpectrumFrame spectrumFrameFromVar (const juce::var& v) { return frameFromVar (v); }

private:
    static juce::var venueToVar (const Venue& venue);
    static Venue venueFromVar (const juce::var& v);
    static juce::var snapshotToVar (const Snapshot& snapshot);
    static Snapshot snapshotFromVar (const juce::var& v);
    static juce::var frameToVar (const eeq::dsp::SpectrumFrame& frame);
    static eeq::dsp::SpectrumFrame frameFromVar (const juce::var& v);
    std::vector<Venue> venues_;
    int currentVenueIndex = 0;
};

} // namespace eeq::core
