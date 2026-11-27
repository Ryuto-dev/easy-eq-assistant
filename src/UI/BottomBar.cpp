#include "BottomBar.h"

#include "../Core/Log.h"
#include "../DSP/SweepGenerator.h"
#include "Buttons.h"
#include "Strings.h"

namespace eeq::ui
{

BottomBar::BottomBar (eeq::audio::AudioEngine& engineIn)
    : engine (engineIn)
{
    styleAccentButton (measurementButton);
    measurementButton.setButtonText (strings::measurementStartButton);
    measurementButton.onClick = [this]
    {
        auto& measurement = engine.getMeasurement();

        // Space キーと同じ動作（issue #131）: 測定だけを開始/停止し、発振器には触れない
        if (measurement.isMeasuring())
            measurement.stopMeasurement();
        else
            measurement.startMeasurement();
    };
    addAndMakeVisible (measurementButton);

    styleButton (resetButton);
    resetButton.setButtonText (strings::resetAveragesButton);
    resetButton.onClick = [this]
    {
        engine.getMeasurement().resetAverages();
        engine.clearClipLatches();
    };
    addAndMakeVisible (resetButton);

    styleButton (snapshotButton);
    snapshotButton.setButtonText (strings::snapshotButton);
    snapshotButton.onClick = [this]
    {
        if (onSnapshotRequested != nullptr)
            onSnapshotRequested();
    };
    addAndMakeVisible (snapshotButton);

    styleButton (delayButton);
    delayButton.setButtonText (strings::delayMeasureButton);
    delayButton.onClick = [this]
    {
        if (onDelayMeasureRequested != nullptr)
            onDelayMeasureRequested();
    };
    addAndMakeVisible (delayButton);

    styleButton (calibrationButton);
    calibrationButton.setButtonText (strings::calibrationButton);
    calibrationButton.onClick = [this]
    {
        if (onCalibrationRequested != nullptr)
            onCalibrationRequested();
    };
    addAndMakeVisible (calibrationButton);

    // 励起信号（仕様 3.2）: ピンクノイズ（既定）/ スイープ / WAV / 外部入力のみ
    styleCombo (excitationCombo);
    excitationCombo.addItem (strings::excitationPinkNoise, 1);
    excitationCombo.addItem (strings::excitationSweep, 2);
    excitationCombo.addItem (strings::excitationWav, 3);
    excitationCombo.addItem (strings::excitationExternal, 4);
    // 校正用の信号は各タブが切り替える。表示だけに使い、ここからは選ばせない（issue #133）
    excitationCombo.addItem (strings::excitationCalTone, 5);
    excitationCombo.addItem (strings::excitationCalBandNoise, 6);
    excitationCombo.setItemEnabled (5, false);
    excitationCombo.setItemEnabled (6, false);
    excitationCombo.setSelectedId (1, juce::dontSendNotification);
    excitationCombo.onChange = [this] { applyExcitationSource (excitationCombo.getSelectedId()); };
    addAndMakeVisible (excitationCombo);

    styleAccentButton (noiseButton);
    noiseButton.setButtonText (strings::startNoiseButton);
    noiseButton.onClick = [this]
    {
        if (engine.isNoiseAudible())
        {
            eeq::log::write ("excitation button: stop clicked");
            engine.stopExcitation();
        }
        else
        {
            eeq::log::write ("excitation button: start clicked");
            engine.startExcitation();
        }
    };
    addAndMakeVisible (noiseButton);

    levelTitle.setText (strings::outputLevelLabel, juce::dontSendNotification);
    levelTitle.setJustificationType (juce::Justification::centredRight);
    styleLabel (levelTitle);
    addAndMakeVisible (levelTitle);

    styleSlider (levelSlider);
    // 上限は「クリップしない最大 RMS」: ピンクノイズのクレスト（約 14.3 dB）ぶんの
    // 余裕を残す（issue #52）。これを超えると生成器の出力が天井でクリップし、
    // 測定そのものが歪むため、スライダーで入れられないようにする
    levelSlider.setRange (-60.0, (double) -eeq::audio::AudioEngine::excitationCrestDb, 0.5);
    levelSlider.setValue (engine.getNoiseLevelDb(), juce::dontSendNotification);
    levelSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 90, 24);
    levelSlider.setTextValueSuffix (" dBFS");
    levelSlider.addListener (this);
    addAndMakeVisible (levelSlider);

    // 発振中インジケータ（issue #141）: 常時目立つ表示（赤地に白抜き）
    oscillatorActiveLabel.setText (strings::oscillatorActiveLabel, juce::dontSendNotification);
    oscillatorActiveLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    oscillatorActiveLabel.setColour (juce::Label::backgroundColourId, theme::get (theme::ColourId::danger));
    oscillatorActiveLabel.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
    oscillatorActiveLabel.setJustificationType (juce::Justification::centred);
    oscillatorActiveLabel.setInterceptsMouseClicks (false, false);
    oscillatorActiveLabel.setVisible (false);
    addAndMakeVisible (oscillatorActiveLabel);

    // ショートカットヒント（issue #141）
    shortcutsLabel.setText (strings::shortcutsHint, juce::dontSendNotification);
    shortcutsLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::textDim));
    shortcutsLabel.setFont (juce::Font (juce::FontOptions (10.0f)));
    shortcutsLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (shortcutsLabel);

    statusLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::textDim));
    statusLabel.setFont (juce::Font (juce::FontOptions (11.5f)));
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);
}

void BottomBar::applyExcitationSource (int comboId)
{
    // 同じ項目の選び直しも受け付ける（表示とエンジンがずれていても選べば必ず反映される: issue #133）
    lastExcitationId = comboId;
    auto& measurement = engine.getMeasurement();

    switch (comboId)
    {
        case 2:
        {
            // ログスイープ（既定 10 秒。長さは IR・遅延タブで変えられる。仕様 3.2: 2〜30秒）
            // ピーク1.0で生成する（issue #29）。レベル設定はオーディオスレッドが
            // 実効RMS基準で一括して掛けるので、ここで焼き込まない
            const auto sampleRate = engine.getCurrentSampleRate();
            auto sweep = eeq::dsp::LogSweepGenerator::generate (sampleRate, 10.0, 20.0, 20000.0, 1.0f);
            engine.setSweep (std::move (sweep));
            break;
        }

        case 3:
        {
            // WAVファイルを選んで読み込む
            auto chooser = std::make_shared<juce::FileChooser> (strings::wavFileChooserTitle,
                                                                juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                                "*.wav;*.aiff;*.aif");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this, chooser] (const juce::FileChooser& fc)
                {
                    const auto file = fc.getResult();

                    if (file == juce::File())
                    {
                        excitationCombo.setSelectedId (1, juce::sendNotificationSync);
                        return;
                    }

                    juce::String error;

                    if (! engine.loadWavExcitation (file, error))
                    {
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                            strings::wavLoadFailed, error, strings::okButton, nullptr, nullptr);
                        excitationCombo.setSelectedId (1, juce::sendNotificationSync);
                    }
                });
            break;
        }

        case 4:
            engine.setExcitationSource (eeq::audio::AudioEngine::ExcitationSource::external);
            break;

        case 1:
        default:
            engine.setExcitationSource (eeq::audio::AudioEngine::ExcitationSource::pinkNoise);
            break;
    }
}

void BottomBar::update()
{
    const auto audible = engine.isNoiseAudible();
    noiseButton.setButtonText (audible ? strings::stopNoiseButton : strings::startNoiseButton);
    noiseButton.setColour (juce::TextButton::buttonColourId,
                           audible ? theme::get (theme::ColourId::danger)
                                   : theme::get (theme::ColourId::buttonOn));

    // 発振中インジケータ（issue #141）: レベルとソースも出す
    oscillatorActiveLabel.setVisible (audible);

    if (audible)
    {
        const auto text = outputStatusText (engine);

        if (oscillatorActiveLabel.getText() != text)
            oscillatorActiveLabel.setText (text, juce::dontSendNotification);
    }

    // 各タブがソースを切り替えても表示を追従させる（issue #133）
    const auto sourceId = comboIdForSource (engine.getExcitationSource());

    if (sourceId != excitationCombo.getSelectedId() && ! excitationCombo.isPopupActive())
    {
        excitationCombo.setSelectedId (sourceId, juce::dontSendNotification);
        lastExcitationId = sourceId;
    }

    const auto measuring = engine.getMeasurement().isMeasuring();
    measurementButton.setButtonText (measuring ? strings::measurementStopButton : strings::measurementStartButton);
    measurementButton.setColour (juce::TextButton::buttonColourId,
                                 measuring ? theme::get (theme::ColourId::danger)
                                           : theme::get (theme::ColourId::buttonOn));

    // 出力レベルは他タブ（リミットテスト等）からも変更されるため、ドラッグ中に
    // なければエンジンの現在値へ同期する
    if (! levelSlider.isMouseButtonDown()
        && std::abs (levelSlider.getValue() - (double) engine.getNoiseLevelDb()) > 0.01)
        levelSlider.setValue ((double) engine.getNoiseLevelDb(), juce::dontSendNotification);
}

void BottomBar::setStatusText (const juce::String& text)
{
    if (statusLabel.getText() != text)
        statusLabel.setText (text, juce::dontSendNotification);
}

void BottomBar::paint (juce::Graphics& g)
{
    g.setColour (theme::get (theme::ColourId::panelBackground));
    g.fillRect (getLocalBounds());
    g.setColour (theme::get (theme::ColourId::controlOutline));
    g.fillRect (getLocalBounds().removeFromTop (1));
}

int BottomBar::comboIdForSource (eeq::audio::AudioEngine::ExcitationSource source) noexcept
{
    using Source = eeq::audio::AudioEngine::ExcitationSource;

    switch (source)
    {
        case Source::sweep:       return 2;
        case Source::wavFile:     return 3;
        case Source::external:    return 4;
        case Source::tone1k:      return 5;
        case Source::bandNoise1k: return 6;
        case Source::pinkNoise:
        default:                  return 1;
    }
}

juce::String BottomBar::outputStatusText (const eeq::audio::AudioEngine& engine)
{
    using Source = eeq::audio::AudioEngine::ExcitationSource;
    juce::String name;

    switch (engine.getExcitationSource())
    {
        case Source::sweep:       name = strings::excitationSweep; break;
        case Source::wavFile:     name = strings::excitationWav; break;
        case Source::external:    name = strings::excitationExternal; break;
        case Source::tone1k:      name = strings::excitationCalTone; break;
        case Source::bandNoise1k: name = strings::excitationCalBandNoise; break;
        case Source::pinkNoise:
        default:                  name = strings::excitationPinkNoise; break;
    }

    return juce::String (u8"● ") + strings::oscillatorActiveLabel + "  " + juce::String (engine.getNoiseLevelDb(), 1)
         + " dBFS  " + name;
}

void BottomBar::resized()
{
    auto bounds = getLocalBounds().reduced (8, 4);

    // 2段組（issue #137）: 上段=操作、下段=出力中バッジ・状態表示・ショートカット。
    // 1段に詰めると最小幅で状態表示（UI停止→自動停止などの安全通知）が数十 px に潰れていた
    auto row2 = bounds.removeFromBottom (18);
    bounds.removeFromBottom (2);
    auto row1 = bounds;

    auto place = [&row1] (juce::Component& c, int width, int height, int gapAfter)
    {
        c.setBounds (row1.removeFromLeft (width).withSizeKeepingCentre (width, height));
        row1.removeFromLeft (gapAfter);
    };

    place (measurementButton, 86, 30, 6);
    place (resetButton, 96, 30, 10);
    place (snapshotButton, 104, 30, 6);
    place (delayButton, 72, 30, 6);
    place (calibrationButton, 56, 30, 12);
    place (excitationCombo, 128, 26, 6);
    place (noiseButton, 104, 30, 12);

    levelTitle.setBounds (row1.removeFromLeft (70));
    row1.removeFromLeft (6);
    const auto sliderWidth = juce::jlimit (0, 260, row1.getWidth());
    levelSlider.setBounds (row1.removeFromLeft (sliderWidth).withSizeKeepingCentre (sliderWidth, 30));

    oscillatorActiveLabel.setBounds (row2.removeFromLeft (juce::jmin (300, row2.getWidth() / 3)));
    row2.removeFromLeft (10);
    shortcutsLabel.setBounds (row2.removeFromRight (juce::jmin (430, row2.getWidth() / 2)));
    row2.removeFromRight (10);
    statusLabel.setBounds (row2);
}

void BottomBar::sliderValueChanged (juce::Slider* slider)
{
    if (slider != &levelSlider)
        return;

    const auto value = (float) levelSlider.getValue();
    const auto limit = eeq::audio::AudioEngine::outputLevelLimitDb;

    if (value <= limit)
    {
        overLimitConfirmed = false;
        engine.setNoiseLevelDb (value);
        return;
    }

    // 上限超え：未承認ならエンジンへは適用せず、スライダー値だけ動かす
    // ドラッグ中は limit でクランプした値を出しておく（音が出ないように）
    if (! overLimitConfirmed)
    {
        if (levelSlider.isMouseButtonDown())
            engine.setNoiseLevelDb (limit);
        // テキスト入力等の即確認の場合は confirmOverLimitIfNeeded で処理
        else
            confirmOverLimitIfNeeded();
    }
    else
    {
        // 承認済みならそのまま適用
        engine.setNoiseLevelDb (value);
    }
}

void BottomBar::sliderDragEnded (juce::Slider* slider)
{
    if (slider == &levelSlider)
        confirmOverLimitIfNeeded();
}

void BottomBar::confirmOverLimitIfNeeded()
{
    const auto value = (float) levelSlider.getValue();
    const auto limit = eeq::audio::AudioEngine::outputLevelLimitDb;

    if (value <= limit || overLimitConfirmed || dialogOpen)
        return;

    dialogOpen = true;
    juce::Component::SafePointer<BottomBar> safeThis { this };

    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon,
        strings::overLimitTitle, strings::overLimitMessage,
        strings::overLimitOk, strings::overLimitCancel, this,
        juce::ModalCallbackFunction::create ([safeThis, value, limit] (int result) mutable
        {
            if (safeThis == nullptr)
                return;

            safeThis->dialogOpen = false;

            if (result == 0)
            {
                // キャンセル：−20 dBFS に戻す
                safeThis->levelSlider.setValue ((double) limit, juce::sendNotificationSync);
            }
            else
            {
                // OK：承認フラグを立てて、エンジンへ適用
                safeThis->overLimitConfirmed = true;
                safeThis->engine.setNoiseLevelDb (value);
            }
        }));
}

} // namespace eeq::ui
