#pragma once

#include <juce_core/juce_core.h>

#include <vector>

// Ref/Meas のペアストリームを、オーディオスレッドから解析スレッドへ渡す
// ロックフリーのリングバッファ（仕様 2. の (1)→(2)）。
// SPSC（1書き手・1読み手）前提。解析が遅れた分は捨てる（平均は続行可能）。

namespace eeq::dsp
{

class SampleRingBuffer
{
public:
    explicit SampleRingBuffer (int capacityFrames)
        : fifo (capacityFrames),
          refData ((size_t) capacityFrames, 0.0f),
          measData ((size_t) capacityFrames, 0.0f)
    {
    }

    /** オーディオスレッドから呼ぶ。確保・ロックなし */
    void write (const float* refSamples, const float* measSamples, int numSamples) noexcept
    {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo.prepareToWrite (numSamples, start1, size1, start2, size2);

        copyInto (refData.data(), refSamples, start1, size1, start2, size2);
        copyInto (measData.data(), measSamples, start1, size1, start2, size2);

        fifo.finishedWrite (size1 + size2);
    }

    /** 解析スレッドから呼ぶ。実際に読めたサンプル数を返す */
    int read (float* refOut, float* measOut, int maxSamples) noexcept
    {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo.prepareToRead (maxSamples, start1, size1, start2, size2);

        copyOut (refData.data(), refOut, start1, size1, start2, size2);
        copyOut (measData.data(), measOut, start1, size1, start2, size2);

        fifo.finishedRead (size1 + size2);
        return size1 + size2;
    }

    int samplesAvailable() const noexcept { return (int) fifo.getNumReady(); }

    void reset() noexcept { fifo.reset(); }

private:
    static void copyInto (float* dest, const float* src, int start1, int size1, int start2, int size2) noexcept
    {
        for (int i = 0; i < size1; ++i)
            dest[start1 + i] = src[i];

        for (int i = 0; i < size2; ++i)
            dest[start2 + i] = src[size1 + i];
    }

    static void copyOut (const float* src, float* dest, int start1, int size1, int start2, int size2) noexcept
    {
        for (int i = 0; i < size1; ++i)
            dest[i] = src[start1 + i];

        for (int i = 0; i < size2; ++i)
            dest[size1 + i] = src[start2 + i];
    }

    juce::AbstractFifo fifo;
    std::vector<float> refData;
    std::vector<float> measData;
};

} // namespace eeq::dsp
