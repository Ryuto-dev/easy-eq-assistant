// 監査（issue #127〜#150）で見つかった「部品は正しいのにつなぐと間違う」種類の不具合の回帰テスト。
// 値を範囲ではなく数値で固定する。

#include "gtest/gtest.h"

#include "../src/Core/ProjectStore.h"
#include "../src/DSP/OctaveSmoother.h"
#include "../src/DSP/TransferFunctionAccumulator.h"
#include "../src/UI/Buttons.h"
#include "../src/UI/FrequencyGraphView.h"

#include <cmath>
#include <complex>
#include <random>
#include <vector>

using namespace eeq::dsp;

namespace
{
constexpr double pi = juce::MathConstants<double>::pi;

// 遅延 D サンプル・ゲイン 1 の理想的な系のフレーム（bin ごとに H = e^{-j2πkD/N}）
SpectrumFrame makeDelayFrame (int fftSize, double sampleRate, double delaySamples)
{
    SpectrumFrame frame;
    frame.fftSize = fftSize;
    frame.sampleRate = sampleRate;
    frame.averages = 16;
    const auto bins = (size_t) fftSize / 2 + 1;
    frame.transfer.resize (bins);
    frame.magnitudeDb.assign (bins, 0.0f);
    frame.phaseDeg.resize (bins);
    frame.coherence.assign (bins, 1.0f);
    frame.refPowerDb.assign (bins, 0.0f);
    frame.measPowerDb.assign (bins, 0.0f);

    for (size_t k = 0; k < bins; ++k)
    {
        const auto phase = -2.0 * pi * (double) k * delaySamples / (double) fftSize;
        frame.transfer[k] = std::polar (1.0f, (float) phase);
        frame.phaseDeg[k] = (float) (std::remainder (phase, 2.0 * pi) * 180.0 / pi);
    }

    return frame;
}
} // namespace

// ---- issue #107: 位相は平滑化の「前」に遅延補正する ----
TEST (MeasurementRegression, SmoothingWithDelayCompensationKeepsPhaseFlat)
{
    // 10 ms の遅延（1/6 oct 帯域の中で位相が何回転もする）
    const auto frame = makeDelayFrame (32768, 48000.0, 480.0);
    const auto compensated = smoothToLogGrid (frame, 6, 100.0, 16000.0, 200, 80.0f, 480.0);

    for (const auto& p : compensated)
    {
        ASSERT_GT (p.count, 0);
        EXPECT_NEAR (p.phaseDeg, 0.0f, 0.5f) << "f=" << p.freq;
        EXPECT_NEAR (p.magnitudeDb, 0.0f, 0.01f) << "f=" << p.freq;
        EXPECT_NEAR (std::abs (p.h), 1.0, 1.0e-3) << "f=" << p.freq;
    }

    // 補正しないで平均すると、10 kHz では単位ベクトルの平均がほぼ打ち消し合う
    // （旧実装は平均後に中心周波数だけ回していたので、位相表示が崩れていた）
    const auto raw = smoothToLogGrid (frame, 6, 9000.0, 11000.0, 4);
    const auto& mid = raw[2];
    const auto rotated = std::arg (mid.h * std::polar (1.0, 2.0 * pi * mid.freq * 480.0 / 48000.0)) * 180.0 / pi;
    EXPECT_GT (std::abs (rotated), 2.0); // 後から回しても 0° に戻らない（=補正の順序が重要）
}

// ---- issue #119 (5): 帯域が 1 bin より狭いときは最も近い bin を使う ----
TEST (MeasurementRegression, NarrowBandUsesNearestBin)
{
    SpectrumFrame frame = makeDelayFrame (8192, 48000.0, 0.0);
    const auto binWidth = 48000.0 / 8192.0; // 5.86 Hz

    // 振幅を bin 番号で区別できるようにする
    for (size_t k = 0; k < frame.transfer.size(); ++k)
        frame.transfer[k] = { (float) k, 0.0f };

    // 1/48 oct・20〜40 Hz は帯域幅 < 1 bin。中心 fc に最も近い bin を使っていること
    const auto points = smoothToLogGrid (frame, 48, 20.0, 40.0, 10);

    for (const auto& p : points)
    {
        const auto expectedBin = std::lround (p.freq / binWidth);
        const auto gotBin = std::lround (std::sqrt (std::pow (10.0, p.magnitudeDb / 10.0)));
        EXPECT_EQ (gotBin, expectedBin) << "fc=" << p.freq;
    }
}

// ---- issue #147: FIFO 平均は直近 N フレームの等重み平均 ----
TEST (MeasurementRegression, FifoAveragingForgetsOldFramesExactly)
{
    constexpr int fftSize = 1024;
    TransferFunctionAccumulator acc;
    AveragingConfig config;
    config.mode = AveragingConfig::Mode::fifo;
    config.maxAverages = 4;
    acc.configure (fftSize, 48000.0, config, juce::dsp::WindowingFunction<float>::hann);

    std::mt19937 rng (1);
    std::normal_distribution<float> noise (0.0f, 0.3f);
    std::vector<float> ref (fftSize / 2), meas (fftSize / 2);

    auto feedHop = [&] (float gain)
    {
        for (int i = 0; i < fftSize / 2; ++i)
        {
            ref[(size_t) i] = noise (rng);
            meas[(size_t) i] = gain * ref[(size_t) i];
        }

        acc.feedSamples (ref.data(), meas.data(), fftSize / 2);
    };

    // ゲイン 0.25 のフレームを十分に流したあと、ゲイン 1 に切り替える
    for (int i = 0; i < 20; ++i)
        feedHop (0.25f);

    SpectrumFrame before;
    acc.copyResult (before);
    EXPECT_EQ (before.averages, 4);
    EXPECT_NEAR (std::abs (before.transfer[100]), 0.25, 1.0e-3);

    // 切り替え後、窓がまたがるフレーム（1つ）を含めて N+1 回で古いフレームが完全に抜ける
    for (int i = 0; i < 6; ++i)
        feedHop (1.0f);

    SpectrumFrame after;
    acc.copyResult (after);
    EXPECT_EQ (after.averages, 4);
    EXPECT_NEAR (std::abs (after.transfer[100]), 1.0, 1.0e-3);
    EXPECT_NEAR (after.coherence[100], 1.0f, 1.0e-3f);

    // 指数平均では同じ回数では抜けきらない（FIFO との違いを固定する）
    TransferFunctionAccumulator expo;
    config.mode = AveragingConfig::Mode::exponential;
    expo.configure (fftSize, 48000.0, config, juce::dsp::WindowingFunction<float>::hann);
    rng.seed (1);

    auto feedExpo = [&] (float gain)
    {
        for (int i = 0; i < fftSize / 2; ++i)
        {
            ref[(size_t) i] = noise (rng);
            meas[(size_t) i] = gain * ref[(size_t) i];
        }

        expo.feedSamples (ref.data(), meas.data(), fftSize / 2);
    };

    for (int i = 0; i < 20; ++i)
        feedExpo (0.25f);

    for (int i = 0; i < 6; ++i)
        feedExpo (1.0f);

    SpectrumFrame expoAfter;
    expo.copyResult (expoAfter);
    EXPECT_LT (std::abs (expoAfter.transfer[100]), 0.9);
}

TEST (MeasurementRegression, FifoAveragingStaysExactOverManyFrames)
{
    // 加算と減算を繰り返しても丸め誤差が溜まらない（一巡ごとに総和を取り直す）
    constexpr int fftSize = 512;
    TransferFunctionAccumulator acc;
    AveragingConfig config;
    config.mode = AveragingConfig::Mode::fifo;
    config.maxAverages = 8;
    acc.configure (fftSize, 48000.0, config, juce::dsp::WindowingFunction<float>::hann);

    std::mt19937 rng (7);
    std::normal_distribution<float> noise (0.0f, 1.0f);
    std::vector<float> ref (4096), meas (4096);

    for (int block = 0; block < 200; ++block)
    {
        for (size_t i = 0; i < ref.size(); ++i)
        {
            ref[i] = noise (rng) * (block % 2 == 0 ? 100.0f : 0.01f); // 大きさが極端に違うフレームを交互に
            meas[i] = 0.5f * ref[i];
        }

        acc.feedSamples (ref.data(), meas.data(), (int) ref.size());
    }

    SpectrumFrame result;
    acc.copyResult (result);

    for (size_t k = 5; k < result.transfer.size() - 5; k += 17)
        EXPECT_NEAR (std::abs (result.transfer[k]), 0.5, 1.0e-3) << "bin " << k;
}

// ---- issue #143: スナップショットの表示属性が .eea を往復する ----
TEST (MeasurementRegression, SnapshotDisplayFieldsRoundTrip)
{
    eeq::core::ProjectStore store;
    eeq::core::Snapshot snap;
    snap.name = "Main @FOH";
    snap.colour = juce::Colours::red;
    snap.dbOffset = 1.5;
    snap.delayOffset = 48.0;
    snap.smoothingOverride = 12;
    snap.showPhase = true;
    snap.visible = false;
    snap.frame = makeDelayFrame (1024, 48000.0, 3.0);
    store.snapshots().push_back (snap);

    const auto file = juce::File::createTempFile (".eea");
    ASSERT_TRUE (store.saveToFile (file).isEmpty());

    eeq::core::ProjectStore loaded;
    ASSERT_TRUE (loaded.loadFromFile (file).isEmpty());
    file.deleteFile();

    ASSERT_EQ (loaded.snapshots().size(), 1u);
    const auto& s = loaded.snapshots().front();
    EXPECT_EQ (s.name, snap.name);
    EXPECT_EQ (s.colour, snap.colour);
    EXPECT_DOUBLE_EQ (s.dbOffset, 1.5);
    EXPECT_DOUBLE_EQ (s.delayOffset, 48.0);
    EXPECT_EQ (s.smoothingOverride, 12);
    EXPECT_TRUE (s.showPhase);
    EXPECT_FALSE (s.visible);
    EXPECT_EQ (s.frame.fftSize, 1024);
    EXPECT_NE (s.id, snap.id); // 読み込んだスナップショットは新しい id（表示キャッシュを取り違えない）
}

// ---- UI: グラフの段構成（issue #144）と重ね描き（issue #127）、パン（issue #129） ----
class GraphTest : public ::testing::Test
{
protected:
    juce::ScopedJuceInitialiser_GUI gui;
};

TEST_F (GraphTest, ThreePanelLayoutHasNonEmptyPhaseAndCoherence)
{
    eeq::ui::FrequencyGraphView graph;
    graph.setSize (800, 500);

    graph.setViewMode (eeq::ui::ViewMode::ampPhaseCoh);
    const auto amp = graph.getAmplitudeAreaForTest();
    const auto phase = graph.getPhaseAreaForTest();
    const auto coh = graph.getCoherenceAreaForTest();

    EXPECT_GT (amp.getHeight(), 200.0f);
    EXPECT_GT (phase.getHeight(), 80.0f); // 旧実装では高さ 0 になり位相が消えていた
    EXPECT_GT (coh.getHeight(), 50.0f);
    EXPECT_LE (amp.getBottom(), phase.getY());
    EXPECT_LE (phase.getBottom(), coh.getY());

    graph.setViewMode (eeq::ui::ViewMode::ampPhase);
    EXPECT_GT (graph.getPhaseAreaForTest().getHeight(), 150.0f);
    EXPECT_TRUE (graph.getCoherenceAreaForTest().isEmpty());

    graph.setViewMode (eeq::ui::ViewMode::ampOnly);
    EXPECT_TRUE (graph.getPhaseAreaForTest().isEmpty());
    EXPECT_FLOAT_EQ (graph.getAmplitudeAreaForTest().getHeight(), graph.getPlotAreaForTest().getHeight());
}

TEST_F (GraphTest, OverlayTracesAreDrawableWithoutLiveData)
{
    eeq::ui::FrequencyGraphView graph;
    graph.setSize (800, 500);
    EXPECT_FALSE (graph.hasAnythingToDraw());

    auto columns = std::make_shared<std::vector<eeq::ui::FrequencyGraphView::OverlayColumn>>();
    columns->push_back ({ 100.0, { 1.0f, 0.0f } });
    columns->push_back ({ 1000.0, { 0.5f, 0.0f } });

    eeq::ui::FrequencyGraphView::OverlayTrace trace;
    trace.name = "Before";
    trace.colour = juce::Colours::orange;
    trace.columns = columns;
    graph.setOverlayTraces ({ trace });

    // ライブ測定がなくても、保存済みスナップショットは表示される（issue #130）
    EXPECT_EQ (graph.getNumOverlayTraces(), 1u);
    EXPECT_TRUE (graph.hasAnythingToDraw());

    // 描画しても落ちない（列は周波数で配置される）
    juce::Image image (juce::Image::ARGB, 800, 500, true);
    juce::Graphics g (image);
    graph.paintEntireComponent (g, false);
}

TEST_F (GraphTest, HorizontalDragPansByTheDraggedDistanceOnly)
{
    eeq::ui::FrequencyGraphView graph;
    graph.setSize (1000, 500);
    auto& axis = graph.getAxisForTest();
    axis.viewMin = 100.0;
    axis.viewMax = 10000.0;

    const auto plot = graph.getPlotAreaForTest();
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto y = plot.getCentreY();
    const auto startX = plot.getCentreX();

    auto event = [&] (float x)
    {
        return juce::MouseEvent (source, { x, y }, juce::ModifierKeys::leftButtonModifier, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &graph, &graph, juce::Time::getCurrentTime(), { startX, y },
                                 juce::Time::getCurrentTime(), 1, false);
    };

    graph.mouseDown (event (startX));

    // 10 px ずつ 10 回ドラッグ = 合計 100 px。旧実装は毎回「開始点からの累積」を足していたので加速した
    for (int i = 1; i <= 10; ++i)
        graph.mouseDrag (event (startX + 10.0f * (float) i));

    graph.mouseUp (event (startX + 100.0f));

    const auto octavesPerPixel = std::log2 (100.0) / plot.getWidth();
    const auto expectedShiftOctaves = -100.0 * octavesPerPixel;
    EXPECT_NEAR (std::log2 (axis.viewMin / 100.0), expectedShiftOctaves, 1.0e-6);
    EXPECT_NEAR (std::log2 (axis.viewMax / axis.viewMin), std::log2 (100.0), 1.0e-9);
}

// ---- issue #101: 入力欄は Esc を消費しない（緊急停止が親へ届く） ----
TEST_F (GraphTest, StyledTextEditorDoesNotSwallowEscape)
{
    juce::TextEditor editor;
    eeq::ui::styleTextEditor (editor);
    editor.setText ("12.5");

    EXPECT_FALSE (editor.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    EXPECT_FALSE (editor.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)));
}
