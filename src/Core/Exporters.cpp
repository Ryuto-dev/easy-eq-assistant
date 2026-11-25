#include "Exporters.h"

#include <cmath>
#include <fstream>

namespace eeq::core::exporters
{

namespace
{
std::ofstream openUtf8 (const juce::File& file)
{
    // BOM 付き UTF-8 にすると Excel でも文字化けしない
    std::ofstream stream (file.getFullPathName().toStdString(), std::ios::binary);

    if (stream.is_open())
    {
        const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
        stream.write ((const char*) bom, 3);
    }

    return stream;
}
} // namespace

juce::String writeSnapshotCsv (const eeq::dsp::SpectrumFrame& frame, const juce::File& file)
{
    std::ofstream stream = openUtf8 (file);

    if (! stream.is_open())
        return juce::String (u8"ファイルに書き込めませんでした: ") + file.getFullPathName();

    stream << "frequency_hz,magnitude_db,phase_deg,coherence\n";
    stream << std::fixed;

    const auto halfBins = (int) frame.magnitudeDb.size();
    const auto binWidth = frame.sampleRate / (double) frame.fftSize;

    for (int k = 1; k < halfBins; ++k)
    {
        const auto freq = (double) k * binWidth;

        if (freq < 10.0 || freq > 24000.0)
            continue;

        stream << freq << ',' << frame.magnitudeDb[(size_t) k] << ','
               << frame.phaseDeg[(size_t) k] << ',' << frame.coherence[(size_t) k] << '\n';
    }

    return {};
}

juce::String writeSnapshotRewText (const eeq::dsp::SpectrumFrame& frame, const juce::String& curveName,
                                   const juce::File& file)
{
    std::ofstream stream = openUtf8 (file);

    if (! stream.is_open())
        return juce::String (u8"ファイルに書き込めませんでした: ") + file.getFullPathName();

    stream << "* Easy EQ assistant\n";
    stream << "* " << curveName.toStdString() << "\n";
    stream << "* Frequency (Hz)\tLevel (dB)\tPhase (deg)\n\n";
    stream << std::fixed;

    const auto halfBins = (int) frame.magnitudeDb.size();
    const auto binWidth = frame.sampleRate / (double) frame.fftSize;

    for (int k = 1; k < halfBins; ++k)
    {
        const auto freq = (double) k * binWidth;

        if (freq < 10.0 || freq > 24000.0)
            continue;

        stream << freq << '\t' << frame.magnitudeDb[(size_t) k] << '\t'
               << frame.phaseDeg[(size_t) k] << '\n';
    }

    return {};
}

juce::String writeImpulseResponseWav (const eeq::dsp::ImpulseResponse& ir, const juce::File& file)
{
    if (ir.samples.empty())
        return juce::String (u8"インパルス応答がありません");

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wavFormat.createWriterFor (new juce::FileOutputStream (file),
                                   ir.sampleRate, 1, 32, {}, 0));

    if (writer == nullptr)
        return juce::String (u8"ファイルに書き込めませんでした: ") + file.getFullPathName();

    const auto* samples = ir.samples.data();
    writer->writeFromFloatArrays (&samples, 1, (int) ir.samples.size());
    writer.reset(); // flush

    return {};
}

juce::String writeEQProposalCsv (const std::vector<eeq::dsp::peq::Band>& bands, const juce::File& file)
{
    std::ofstream stream = openUtf8 (file);

    if (! stream.is_open())
        return juce::String (u8"ファイルに書き込めませんでした: ") + file.getFullPathName();

    stream << "type,frequency_hz,gain_db,q\n";
    stream << std::fixed;

    for (const auto& band : bands)
    {
        const char* type = "bell";

        switch (band.type)
        {
            case eeq::dsp::peq::Band::Type::bell: type = "bell"; break;
            case eeq::dsp::peq::Band::Type::lowShelf: type = "low_shelf"; break;
            case eeq::dsp::peq::Band::Type::highShelf: type = "high_shelf"; break;
            case eeq::dsp::peq::Band::Type::lowPass: type = "low_pass"; break;
            case eeq::dsp::peq::Band::Type::highPass: type = "high_pass"; break;
        }

        stream << type << ',' << band.frequency << ',' << band.gainDb << ',' << band.q << '\n';
    }

    return {};
}

juce::String writeEQProposalText (const std::vector<eeq::dsp::peq::Band>& bands, const juce::File& file)
{
    std::ofstream stream = openUtf8 (file);

    if (! stream.is_open())
        return juce::String (u8"ファイルに書き込めませんでした: ") + file.getFullPathName();

    stream << juce::String (u8"Easy EQ assistant - 補正EQ案\n").toStdString();
    stream << juce::String (u8"（参考値。ミキサーへの手入力用の控えです）\n\n").toStdString();

    int index = 1;

    for (const auto& band : bands)
    {
        const char* type = "Bell";

        switch (band.type)
        {
            case eeq::dsp::peq::Band::Type::bell: type = "Bell"; break;
            case eeq::dsp::peq::Band::Type::lowShelf: type = "LowShelf"; break;
            case eeq::dsp::peq::Band::Type::highShelf: type = "HighShelf"; break;
            case eeq::dsp::peq::Band::Type::lowPass: type = "LPF"; break;
            case eeq::dsp::peq::Band::Type::highPass: type = "HPF"; break;
        }

        stream << "CH? EQ" << index << ": " << type
               << "  " << juce::String (band.frequency, 1).toStdString() << " Hz"
               << "  " << (band.gainDb > 0 ? "+" : "") << juce::String (band.gainDb, 2).toStdString() << " dB"
               << "  Q=" << juce::String (band.q, 2).toStdString()
               << (band.enabled ? "" : "  (OFF)") << "\n";

        ++index;
    }

    if (bands.empty())
        stream << juce::String (u8"（帯域なし）\n").toStdString();

    return {};
}

bool createSplLogCsv (const juce::File& file)
{
    std::ofstream stream = openUtf8 (file);

    if (! stream.is_open())
        return false;

    stream << "time,spl_db,leq_1min_db,leq_15min_db,leq_session_db,peak_db\n";
    return true;
}

bool appendSplLogLine (const juce::File& file, const juce::String& timeText,
                       double splDb, double leq1min, double leq15min, double leqSession, double peakDb)
{
    std::ofstream stream (file.getFullPathName().toStdString(), std::ios::app);

    if (! stream.is_open())
        return false;

    stream << std::fixed;
    stream << timeText.toStdString() << ',' << splDb << ',' << leq1min << ','
           << leq15min << ',' << leqSession << ',' << peakDb << '\n';

    return true;
}

} // namespace eeq::core::exporters
