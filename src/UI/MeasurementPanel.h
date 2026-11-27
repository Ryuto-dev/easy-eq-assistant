#pragma once

#include "Core/JuceModules.h"

#include "../DSP/TransferFunctionAccumulator.h"

namespace eeq::ui
{

// 右パネルの測定設定（仕様 5.1 右パネル、3.1 の測定仕様）。
// FFTサイズ / 窓関数 / 平均方式 / 平均回数 / スムージング。
// スムージングはプリセット選択 + FIFO（指数平均時定数）対応（issue #147）。
class MeasurementPanel : public juce::Component, private juce::ComboBox::Listener, private juce::Slider::Listener
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
    /** 0 = なし、3 = 1/3 oct、48 = 1/48 oct、-1 = FIFO */
    int getSmoothingOctaveDenominator() const noexcept;
    /** FIFO使用時の時定数（秒）を返す。FIFOでない場合は 0 */
    double getFifoTimeConstant() const noexcept;
    void setAverageCountDisplay (int averages);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void comboBoxChanged (juce::ComboBox*) override;
    void sliderValueChanged (juce::Slider*) override;
    void saveSettings();
    void updateSmoothingControls();

    juce::PropertiesFile* settings = nullptr;
    bool loadingSettings = false;
    juce::Label titleLabel;
    juce::Label fftLabel, windowLabel, avgModeLabel, avgCountLabel, smoothingLabel;
    juce::ComboBox fftCombo, windowCombo, avgModeCombo, smoothingCombo;
    juce::Slider avgCountSlider;
    juce::Label averagesDisplay;

    // FIFO 平滑化用（issue #147）
    juce::Label fifoTimeLabel;
    juce::TextEditor fifoTimeEditor;
    bool useFifo = false;

    static constexpr int fftSizes[] = { 8192, 16384, 32768, 65536 };
    // smoothingDenominators: 0=なし, 48=1/48, 24=1/24, 12=1/12, 6=1/6, 3=1/3, 2=1/2, 1=1/1
    static constexpr int smoothingDenominators[] = { 0, 48, 24, 12, 6, 3, 2, 1 };
};

} // namespace eeq::ui
