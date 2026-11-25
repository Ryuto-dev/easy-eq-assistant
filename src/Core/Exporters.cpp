#include "Exporters.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace eeq::core::exporters
{

namespace
{
// Windows の日本語ユーザー名など非ASCIIパスでも確実に書けるよう、
// juce::FileOutputStream を使う（std::ofstream に narrow 文字列を渡すと
// ANSI コードページ解釈で非ASCIIパスが壊れる）。BOM 付き UTF-8 にすると
// Excel でも文字化けしない
class TextFileWriter
{
public:
    explicit TextFileWriter (const juce::File& file)
        : stream (file.createOutputStream())
    {
        if (stream != nullptr && stream->openedOk())
        {
            const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
            stream->write (bom, 3);
            ok = true;
        }
    }

    bool isOpen() const noexcept { return ok; }

    void writeLine (const std::string& line)
    {
        if (stream != nullptr)
            stream->write (line.data(), (size_t) line.size());
    }

    bool finalize() const
    {
        if (stream == nullptr)
            return false;

        stream->flush();
        return stream->getStatus().wasOk();
    }

private:
    std::unique_ptr<juce::FileOutputStream> stream;
    bool ok = false;
};

std::string doubleToStdString (double value, int decimals)
{
    std::ostringstream s;
    s << std::fixed << std::setprecision (decimals) << value;
    return s.str();
}
} // namespace

juce::String writeSnapshotCsv (const eeq::dsp::SpectrumFrame& frame, const juce::File& file)
{
    TextFileWriter writer (file);

    if (! writer.isOpen())
        return juce::String (u8"ファイルに書き込めませんでした: ") + file.getFullPathName();

    writer.writeLine ("frequency_hz,magnitude_db,phase_deg,coherence\n");

    const auto halfBins = (int) frame.magnitudeDb.size();
    const auto binWidth = frame.sampleRate / (double) frame.fftSize;

    for (int k = 1; k < halfBins; ++k)
    {
        const auto freq = (double) k * binWidth;

        if (freq < 10.0 || freq > 24000.0)
            continue;

        writer.writeLine (doubleToStdString (freq, 2) + ","
                          + doubleToStdString (frame.magnitudeDb[(size_t) k], 3) + ","
                          + doubleToStdString (frame.phaseDeg[(size_t) k], 3) + ","
                          + doubleToStdString (frame.coherence[(size_t) k], 4) + "\n");
    }

    return writer.finalize() ? juce::String()
                             : juce::String (u8"ファイルへの書き込みに失敗しました: ") + file.getFullPathName();
}

juce::String writeSnapshotRewText (const eeq::dsp::SpectrumFrame& frame, const juce::String& curveName,
                                   const juce::File& file)
{
    TextFileWriter writer (file);

    if (! writer.isOpen())
        return juce::String (u8"ファイルに書き込めませんでした: ") + file.getFullPathName();

    writer.writeLine ("* Easy EQ assistant\n");
    writer.writeLine ("* " + curveName.toStdString() + "\n");
    writer.writeLine ("* Frequency (Hz)\tLevel (dB)\tPhase (deg)\n\n");

    const auto halfBins = (int) frame.magnitudeDb.size();
    const auto binWidth = frame.sampleRate / (double) frame.fftSize;

    for (int k = 1; k < halfBins; ++k)
    {
        const auto freq = (double) k * binWidth;

        if (freq < 10.0 || freq > 24000.0)
            continue;

        writer.writeLine (doubleToStdString (freq, 2) + "\t"
                          + doubleToStdString (frame.magnitudeDb[(size_t) k], 3) + "\t"
                          + doubleToStdString (frame.phaseDeg[(size_t) k], 3) + "\n");
    }

    return writer.finalize() ? juce::String()
                             : juce::String (u8"ファイルへの書き込みに失敗しました: ") + file.getFullPathName();
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
    TextFileWriter writer (file);

    if (! writer.isOpen())
        return juce::String (u8"ファイルに書き込めませんでした: ") + file.getFullPathName();

    writer.writeLine ("type,frequency_hz,gain_db,q\n");

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

        writer.writeLine (std::string (type) + ","
                          + doubleToStdString (band.frequency, 2) + ","
                          + doubleToStdString (band.gainDb, 3) + ","
                          + doubleToStdString (band.q, 3) + "\n");
    }

    return writer.finalize() ? juce::String()
                             : juce::String (u8"ファイルへの書き込みに失敗しました: ") + file.getFullPathName();
}

juce::String writeEQProposalText (const std::vector<eeq::dsp::peq::Band>& bands, const juce::File& file)
{
    TextFileWriter writer (file);

    if (! writer.isOpen())
        return juce::String (u8"ファイルに書き込めませんでした: ") + file.getFullPathName();

    writer.writeLine (juce::String (u8"Easy EQ assistant - 補正EQ案\n").toStdString());
    writer.writeLine (juce::String (u8"（参考値。ミキサーへの手入力用の控えです）\n\n").toStdString());

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

        writer.writeLine ("CH? EQ" + std::to_string (index) + ": " + type
                          + "  " + juce::String (band.frequency, 1).toStdString() + " Hz"
                          + "  " + (band.gainDb > 0 ? "+" : "") + juce::String (band.gainDb, 2).toStdString() + " dB"
                          + "  Q=" + juce::String (band.q, 2).toStdString()
                          + (band.enabled ? "" : "  (OFF)") + "\n");

        ++index;
    }

    if (bands.empty())
        writer.writeLine (juce::String (u8"（帯域なし）\n").toStdString());

    return writer.finalize() ? juce::String()
                             : juce::String (u8"ファイルへの書き込みに失敗しました: ") + file.getFullPathName();
}

bool createSplLogCsv (const juce::File& file)
{
    if (! file.getParentDirectory().exists())
        file.getParentDirectory().createDirectory();

    TextFileWriter writer (file);

    if (! writer.isOpen())
        return false;

    writer.writeLine ("time,spl_db,leq_1min_db,leq_15min_db,leq_session_db,peak_db\n");
    return writer.finalize();
}

bool appendSplLogLine (const juce::File& file, const juce::String& timeText,
                       double splDb, double leq1min, double leq15min, double leqSession, double peakDb)
{
    // 追記は juce::FileOutputStream の末尾へ（非ASCIIパス対応）
    juce::FileOutputStream stream (file);

    if (! stream.openedOk())
        return false;

    stream.setPosition (file.getSize());
    stream.write (timeText.toUTF8(), (size_t) timeText.getNumBytesAsUTF8());

    const auto appendField = [&stream] (double value, const char* separator)
    {
        const auto text = juce::String (value, 2).toStdString();
        stream.write (separator, std::string (separator).size());
        stream.write (text.data(), text.size());
    };

    appendField (splDb, ",");
    appendField (leq1min, ",");
    appendField (leq15min, ",");
    appendField (leqSession, ",");
    appendField (peakDb, ",");
    stream.write ("\n", 1);

    stream.flush();
    return stream.getStatus().wasOk();
}

} // namespace eeq::core::exporters
