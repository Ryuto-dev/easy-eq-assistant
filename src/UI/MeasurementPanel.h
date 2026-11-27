#pragma once

#include "Core/JuceModules.h"

#include "../DSP/TransferFunctionAccumulator.h"

namespace eeq::ui
{

// 右パネルの測定設定（仕様 5.1 右パネル、3.1 の測定仕様）。
// FFTサイズ / 窓関数 / 平均方式（累積・指数・FIFO） / 平均回数（プリセット） / スムージング。
// 既定値はこのクラスの defaults で一か所に定義する（issue #134）。
class MeasurementPanel : public juce::Component, private juce::ComboBox::Listener
{
public:
    MeasurementPanel();

    /** 設定の保存先（起動時に復元し、変更時に保存する） */
    void setSettings (juce::PropertiesFile* settingsToUse);

    /** いずれかの設定が変わったときに呼ばれる */
    std::function<void()> onSettingsChanged;

    int getFftSize() const noexcept;
    juce::dsp::WindowingFunction<float>::WindowingMethod getWindowMethod() const noexcept;
    eeq::dsp::AveragingConfig getAveragingConfig() const noexcept;
    /** 0 = なし、3 = 1/3 oct、48 = 1/48 oct */
    int getSmoothingOctaveDenominator() const noexcept;
    void setAverageCountDisplay (int averages);

    void paint (juce::Graphics&) override;
    void resized() override;

    // 既定値（issue #134: ここだけで定義する）。FIFO 16 回・1/6 oct・16384点（Smaart の既定に近い応答重視）
    struct Defaults
    {
        static constexpr int fftId = 2;        // 16384
        static constexpr int windowId = 1;     // Hann
        static constexpr int avgModeId = 3;    // FIFO
        static constexpr int smoothingId = 5;  // 1/6 oct
        static constexpr int avgCount = 16;
    };

    static constexpr int fftSizes[] = { 8192, 16384, 32768, 65536 };
    static constexpr int smoothingDenominators[] = { 0, 48, 24, 12, 6, 3, 2, 1 }; // id 1..8
    static constexpr int averageCounts[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256 };

private:
    void comboBoxChanged (juce::ComboBox*) override;
    void saveSettings();
    void updateCountItems();

    juce::PropertiesFile* settings = nullptr;
    bool loadingSettings = false;

    juce::Label titleLabel, fftLabel, windowLabel, avgModeLabel, avgCountLabel, smoothingLabel;
    juce::ComboBox fftCombo, windowCombo, avgModeCombo, smoothingCombo, avgCountCombo;
    juce::Label averagesDisplay;
};

} // namespace eeq::ui
