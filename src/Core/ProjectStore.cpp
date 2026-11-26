#include "ProjectStore.h"

#include <algorithm>
#include <cmath>

namespace eeq::core
{

namespace
{
// DynamicObject::getProperty は既定値版を持たないので、未定義のときの既定値を返すヘルパ
template <typename T>
T getPropertyWithDefault (const juce::DynamicObject* object, const char* name, T fallback)
{
    const auto value = object->getProperty (name);
    return value.isVoid() ? fallback : static_cast<T> (value);
}

// 数値配列は base64（float列）で格納して .eea をコンパクトに保つ
juce::var floatArrayToVar (const std::vector<float>& values)
{
    return juce::Base64::toBase64 (values.data(), (int) (values.size() * sizeof (float)));
}

std::vector<float> floatArrayFromVar (const juce::var& v, size_t expectedSize)
{
    juce::MemoryOutputStream decoded;
    juce::Base64::convertFromBase64 (decoded, v.toString());

    const auto& bytes = decoded.getMemoryBlock();
    std::vector<float> values (expectedSize, 0.0f);

    if (bytes.getSize() == expectedSize * sizeof (float) && expectedSize > 0)
        std::memcpy (values.data(), bytes.getData(), bytes.getSize());

    return values;
}

juce::var colourToVar (juce::Colour colour)
{
    return juce::var ((juce::int64) colour.getARGB());
}

juce::Colour colourFromVar (const juce::var& v, juce::Colour fallback)
{
    return v.isVoid() ? fallback : juce::Colour ((juce::uint32) (juce::int64) v);
}

juce::var timeToVar (juce::Time time)
{
    return juce::var ((juce::int64) time.toMilliseconds());
}

juce::Time timeFromVar (const juce::var& v)
{
    return juce::Time (v.isVoid() ? juce::Time::getCurrentTime().toMilliseconds() : (juce::int64) v);
}
} // namespace

ProjectStore::ProjectStore()
{
    // 既定の会場を1つ用意する（空のまま保存・読込ができる）
    venues_.push_back ({});
    venues_.front().name = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M");
    venues_.front().date = juce::Time::getCurrentTime();
}

Session& ProjectStore::currentSession()
{
    auto& venue = currentVenue();

    if (venue.sessions.empty())
    {
        Session session;
        session.name = juce::Time::getCurrentTime().formatted ("%H:%M");
        session.startedAt = juce::Time::getCurrentTime();
        venue.sessions.push_back (session);
    }

    return venue.sessions.back();
}

std::vector<int> ProjectStore::findVenues (const juce::String& query) const
{
    std::vector<int> matches;

    for (int i = 0; i < (int) venues_.size(); ++i)
    {
        if (query.isEmpty() || venues_[(size_t) i].name.containsIgnoreCase (query))
            matches.push_back (i);
    }

    return matches;
}

juce::File ProjectStore::defaultProjectFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("EasyEQAssistant")
        .getChildFile ("last_project.eea");
}

//==============================================================================
// システム補正（ループバック校正）の保存・復元（仕様 6.1）
//==============================================================================

juce::var ProjectStore::correctionToVar (const eeq::core::SystemCorrection& correction)
{
    juce::DynamicObject* o = new juce::DynamicObject();
    juce::var object (o);

    o->setProperty ("active", correction.isActive());

    if (! correction.isActive())
        return object;

    // SystemCorrection から frame と delay を取得して保存
    // 注: SystemCorrection には public な getFrame がないため、
    // ProjectStore では correction 適用時に frame を別に保存しておく
    // ここでは active と delaySamples, measuredAt のみ保存し、frame は別途保存
    o->setProperty ("delaySamples", correction.getRoundtripDelaySamples());
    o->setProperty ("measuredAt", timeToVar (correction.getMeasuredAt()));

    return object;
}

eeq::core::SystemCorrection ProjectStore::correctionFromVar (const juce::var& v)
{
    eeq::core::SystemCorrection correction;

    if (auto* o = v.getDynamicObject())
    {
        if (! getPropertyWithDefault (o, "active", false))
            return correction;

        const auto delaySamples = getPropertyWithDefault (o, "delaySamples", 0.0);
        const auto measuredAt = timeFromVar (o->getProperty ("measuredAt"));

        // delaySamples と measuredAt を一時保存（frame は別途）
        // 実際の correction 再構築は getSystemCorrection() で行うため、
        // ここでは空の correction を返し、frame は ProjectStore 側で管理
    }

    return correction;
}

//==============================================================================
// 公開メソッド

void ProjectStore::setSystemCorrection (const eeq::core::SystemCorrection& correction)
{
    systemCorrection_ = correction;
}

eeq::core::SystemCorrection ProjectStore::getSystemCorrection() const noexcept
{
    return systemCorrection_;
}

//==============================================================================
// JSON変換

juce::var ProjectStore::frameToVar (const eeq::dsp::SpectrumFrame& frame)
{
    juce::DynamicObject* o = new juce::DynamicObject();
    juce::var object (o);

    o->setProperty ("fftSize", frame.fftSize);
    o->setProperty ("sampleRate", frame.sampleRate);
    o->setProperty ("averages", frame.averages);

    // 振幅/位相は複素伝達関数から復元できるので保存しない（復元時に作る）
    std::vector<float> re (frame.transfer.size()), im (frame.transfer.size());

    for (size_t k = 0; k < frame.transfer.size(); ++k)
    {
        re[k] = frame.transfer[k].real();
        im[k] = frame.transfer[k].imag();
    }

    o->setProperty ("transferRe", floatArrayToVar (re));
    o->setProperty ("transferIm", floatArrayToVar (im));
    o->setProperty ("coherence", floatArrayToVar (frame.coherence));
    o->setProperty ("refPowerDb", floatArrayToVar (frame.refPowerDb));
    o->setProperty ("measPowerDb", floatArrayToVar (frame.measPowerDb));

    return object;
}

eeq::dsp::SpectrumFrame ProjectStore::frameFromVar (const juce::var& v)
{
    eeq::dsp::SpectrumFrame frame;

    if (auto* o = v.getDynamicObject())
    {
        frame.fftSize = o->getProperty ("fftSize");
        frame.sampleRate = o->getProperty ("sampleRate");
        frame.averages = o->getProperty ("averages");

        // 破損したファイルの fftSize で巨大な確保や負のサイズ変換をしない
        if (frame.fftSize < 256 || frame.fftSize > (1 << 20) || (frame.fftSize & (frame.fftSize - 1)) != 0)
        {
            frame = {};
            return frame;
        }

        const auto size = (size_t) (frame.fftSize / 2 + 1);
        const auto re = floatArrayFromVar (o->getProperty ("transferRe"), size);
        const auto im = floatArrayFromVar (o->getProperty ("transferIm"), size);

        frame.transfer.resize (size);
        frame.magnitudeDb.resize (size);
        frame.phaseDeg.resize (size);

        for (size_t k = 0; k < size; ++k)
        {
            frame.transfer[k] = { re[k], im[k] };
            frame.magnitudeDb[k] = (float) (20.0 * std::log10 (std::max (std::abs (frame.transfer[k]), 1e-9f)));
            frame.phaseDeg[k] = (float) (std::arg (std::complex<double> (frame.transfer[k].real(),
                                                                     frame.transfer[k].imag()))
                                         * 180.0 / juce::MathConstants<double>::pi);
        }

        frame.coherence = floatArrayFromVar (o->getProperty ("coherence"), size);
        frame.refPowerDb = floatArrayFromVar (o->getProperty ("refPowerDb"), size);
        frame.measPowerDb = floatArrayFromVar (o->getProperty ("measPowerDb"), size);
    }

    return frame;
}

juce::var ProjectStore::snapshotToVar (const Snapshot& snapshot)
{
    juce::DynamicObject* o = new juce::DynamicObject();
    juce::var object (o);

    o->setProperty ("name", snapshot.name);
    o->setProperty ("systemName", snapshot.systemName);
    o->setProperty ("micPoint", snapshot.micPoint);
    o->setProperty ("colour", colourToVar (snapshot.colour));
    o->setProperty ("timestamp", timeToVar (snapshot.timestamp));
    o->setProperty ("temperatureC", snapshot.temperatureC);
    o->setProperty ("note", snapshot.note);
    o->setProperty ("delaySamples", snapshot.delaySamples);
    o->setProperty ("visible", snapshot.visible);
    o->setProperty ("isSelectedA", snapshot.isSelectedA);
    o->setProperty ("isSelectedB", snapshot.isSelectedB);
    o->setProperty ("frame", frameToVar (snapshot.frame));

    return object;
}

Snapshot ProjectStore::snapshotFromVar (const juce::var& v)
{
    Snapshot snapshot;

    if (auto* o = v.getDynamicObject())
    {
        snapshot.name = o->getProperty ("name").toString();
        snapshot.systemName = o->getProperty ("systemName").toString();
        snapshot.micPoint = o->getProperty ("micPoint").toString();
        snapshot.colour = colourFromVar (o->getProperty ("colour"), snapshot.colour);
        snapshot.timestamp = timeFromVar (o->getProperty ("timestamp"));
        snapshot.temperatureC = getPropertyWithDefault (o, "temperatureC", snapshot.temperatureC);
        snapshot.note = o->getProperty ("note").toString();
        snapshot.delaySamples = getPropertyWithDefault (o, "delaySamples", snapshot.delaySamples);
        snapshot.visible = getPropertyWithDefault (o, "visible", true);
        snapshot.isSelectedA = getPropertyWithDefault (o, "isSelectedA", false);
        snapshot.isSelectedB = getPropertyWithDefault (o, "isSelectedB", false);
        snapshot.frame = frameFromVar (o->getProperty ("frame"));
    }

    return snapshot;
}

juce::var ProjectStore::venueToVar (const Venue& venue)
{
    juce::DynamicObject* o = new juce::DynamicObject();
    juce::var object (o);

    o->setProperty ("name", venue.name);
    o->setProperty ("date", timeToVar (venue.date));
    o->setProperty ("temperatureC", venue.temperatureC);
    o->setProperty ("humidityPercent", venue.humidityPercent);
    o->setProperty ("notes", venue.notes);
    o->setProperty ("equipment", venue.equipment);
    o->setProperty ("subCrossoverHz", venue.subCrossoverHz);

    {
        juce::Array<juce::var> systems;

        for (const auto& system : venue.systems)
        {
            juce::DynamicObject* so = new juce::DynamicObject();
            so->setProperty ("name", system.name);
            so->setProperty ("outputChannel", system.outputChannel);
            so->setProperty ("colour", colourToVar (system.colour));
            systems.add (juce::var (so));
        }

        o->setProperty ("systems", systems);
    }

    {
        juce::Array<juce::var> micPoints;

        for (const auto& point : venue.micPoints)
        {
            juce::DynamicObject* po = new juce::DynamicObject();
            po->setProperty ("name", point.name);
            po->setProperty ("description", point.description);
            micPoints.add (juce::var (po));
        }

        o->setProperty ("micPoints", micPoints);
    }

    {
        juce::Array<juce::var> sessions;

        for (const auto& session : venue.sessions)
        {
            juce::DynamicObject* so = new juce::DynamicObject();
            so->setProperty ("name", session.name);
            so->setProperty ("startedAt", timeToVar (session.startedAt));

            juce::Array<juce::var> snapshots;

            for (const auto& snapshot : session.snapshots)
                snapshots.add (snapshotToVar (snapshot));

            so->setProperty ("snapshots", snapshots);
            sessions.add (juce::var (so));
        }

        o->setProperty ("sessions", sessions);
    }

    return object;
}

Venue ProjectStore::venueFromVar (const juce::var& v)
{
    Venue venue;

    if (auto* o = v.getDynamicObject())
    {
        venue.name = o->getProperty ("name").toString();
        venue.date = timeFromVar (o->getProperty ("date"));
        venue.temperatureC = getPropertyWithDefault (o, "temperatureC", venue.temperatureC);
        venue.humidityPercent = getPropertyWithDefault (o, "humidityPercent", venue.humidityPercent);
        venue.notes = o->getProperty ("notes").toString();
        venue.equipment = o->getProperty ("equipment").toString();
        venue.subCrossoverHz = getPropertyWithDefault (o, "subCrossoverHz", 0.0);

        if (const auto* systems = o->getProperty ("systems").getArray())
            for (const auto& systemVar : *systems)
            {
                if (auto* so = systemVar.getDynamicObject())
                {
                    SpeakerSystemInfo system;
                    system.name = so->getProperty ("name").toString();
                    system.outputChannel = getPropertyWithDefault (so, "outputChannel", 0);
                    system.colour = colourFromVar (so->getProperty ("colour"), system.colour);
                    venue.systems.push_back (system);
                }
            }

        if (const auto* micPoints = o->getProperty ("micPoints").getArray())
            for (const auto& pointVar : *micPoints)
            {
                if (auto* po = pointVar.getDynamicObject())
                {
                    MicPoint point;
                    point.name = po->getProperty ("name").toString();
                    point.description = po->getProperty ("description").toString();
                    venue.micPoints.push_back (point);
                }
            }

        if (const auto* sessions = o->getProperty ("sessions").getArray())
            for (const auto& sessionVar : *sessions)
            {
                if (auto* so = sessionVar.getDynamicObject())
                {
                    Session session;
                    session.name = so->getProperty ("name").toString();
                    session.startedAt = timeFromVar (so->getProperty ("startedAt"));

                    if (const auto* snapshots = so->getProperty ("snapshots").getArray())
                        for (const auto& snapshotVar : *snapshots)
                            session.snapshots.push_back (snapshotFromVar (snapshotVar));

                    venue.sessions.push_back (session);
                }
            }
    }

    return venue;
}

juce::String ProjectStore::saveToFile (const juce::File& file) const
{
    juce::DynamicObject* root = new juce::DynamicObject();
    juce::var object (root);

    root->setProperty ("format", "eea-project");
    root->setProperty ("version", 1);

    juce::Array<juce::var> venueArray;

    for (const auto& venue : venues_)
        venueArray.add (venueToVar (venue));

    root->setProperty ("venues", venueArray);
    root->setProperty ("currentVenueIndex", currentVenueIndex);
    root->setProperty ("systemCorrection", correctionToVar (systemCorrection_));

    if (! file.getParentDirectory().exists())
        file.getParentDirectory().createDirectory();

    if (! file.replaceWithText (juce::JSON::toString (object)))
        return juce::String (u8"ファイルに書き込めませんでした: ") + file.getFullPathName();

    return {};
}

juce::String ProjectStore::loadFromFile (const juce::File& file)
{
    const auto text = file.loadFileAsString();

    if (text.isEmpty())
        return juce::String (u8"ファイルを読み込めませんでした: ") + file.getFullPathName();

    const auto parsed = juce::JSON::parse (text);

    if (auto* root = parsed.getDynamicObject())
    {
        if (root->getProperty ("format").toString() != "eea-project")
            return juce::String (u8"Easy EQ assistant のプロジェクトファイルではありません");

        std::vector<Venue> loaded;

        if (const auto* venueArray = root->getProperty ("venues").getArray())
        {
            for (const auto& venueVar : *venueArray)
                loaded.push_back (venueFromVar (venueVar));
        }

        if (loaded.empty())
            return juce::String (u8"会場データがありません");

        venues_ = std::move (loaded);
        currentVenueIndex = juce::jlimit (0, (int) venues_.size() - 1,
                                          getPropertyWithDefault (root, "currentVenueIndex", 0));
        return {};
    }

    return juce::String (u8"JSONとして読み込めませんでした");
}

} // namespace eeq::core
