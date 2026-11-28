#include <gtest/gtest.h>

#include "Core/ProjectStore.h"

// ファイル形式のバージョン管理（ロードマップ A-7）
namespace
{
juce::File tempProject()
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("eeq_format_test_" + juce::String (juce::Random::getSystemRandom().nextInt64()));
    dir.createDirectory();
    return dir.getChildFile ("project.eea");
}
}

// 保存 → 読込 → 保存 で中身が変わらない
TEST (ProjectFormat, RoundTripIsStable)
{
    const auto file = tempProject();
    eeq::core::ProjectStore store;
    store.venues()[0].name = u8"テスト会場";
    store.venues()[0].temperatureC = 23.5;
    ASSERT_TRUE (store.saveToFile (file).isEmpty());
    const auto first = file.loadFileAsString();

    eeq::core::ProjectStore loaded;
    ASSERT_TRUE (loaded.loadFromFile (file).isEmpty());
    EXPECT_EQ (loaded.getLoadedVersion(), eeq::core::ProjectStore::formatVersion);
    ASSERT_TRUE (loaded.saveToFile (file).isEmpty());
    EXPECT_EQ (file.loadFileAsString(), first);
    file.getParentDirectory().deleteRecursively();
}

// v1（版の管理を始める前）のファイルを読める
TEST (ProjectFormat, ReadsVersionOne)
{
    const auto file = tempProject();
    file.replaceWithText (R"({"format":"eea-project","version":1,"venues":[{"name":"old","temperatureC":18.0}],"currentVenueIndex":0})");
    eeq::core::ProjectStore store;
    ASSERT_TRUE (store.loadFromFile (file).isEmpty());
    EXPECT_EQ (store.venues()[0].name, "old");
    EXPECT_DOUBLE_EQ (store.venues()[0].temperatureC, 18.0);

    juce::DynamicObject root;
    eeq::core::ProjectStore::migrate (root, 1);
    EXPECT_EQ ((int) root.getProperty ("version"), 2);
    file.getParentDirectory().deleteRecursively();
}

// 新しい版で保存したファイルを開いて保存し直しても、知らない項目と版を消さない（自動保存の静的版でも）
TEST (ProjectFormat, KeepsUnknownFieldsFromNewerVersions)
{
    const auto file = tempProject();
    file.replaceWithText (R"({"format":"eea-project","version":9,"futureRoot":{"a":1},)"
                          R"("venues":[{"name":"v","futureVenueField":[1,2,3]}],"currentVenueIndex":0})");

    eeq::core::ProjectStore store;
    ASSERT_TRUE (store.loadFromFile (file).isEmpty());
    EXPECT_EQ (store.getLoadedVersion(), 9);

    for (int pass = 0; pass < 2; ++pass)
    {
        const auto error = pass == 0 ? store.saveToFile (file)
                                     : eeq::core::ProjectStore::saveVenuesToFile (store.venues(), 0, file, store.getUnknownRootProperties());
        ASSERT_TRUE (error.isEmpty());
        const auto saved = juce::JSON::parse (file.loadFileAsString());
        SCOPED_TRACE (pass);
        EXPECT_EQ ((int) saved["version"], 9);
        EXPECT_FALSE (saved.getDynamicObject()->hasProperty ("_loadedVersion"));
        EXPECT_EQ ((int) saved["futureRoot"]["a"], 1);
        EXPECT_EQ (saved["venues"][0]["futureVenueField"].size(), 3);
        EXPECT_EQ (saved["venues"][0]["name"].toString(), "v");
    }
    file.getParentDirectory().deleteRecursively();
}

// 世代バックアップ: 上書きのたびに増えすぎず（10 分に 1 個）、上限を超えたら古いものから消す
TEST (ProjectFormat, GenerationBackupsAreRateLimitedAndPruned)
{
    const auto file = tempProject();
    eeq::core::ProjectStore store;
    ASSERT_TRUE (store.saveToFile (file).isEmpty()); // 初回は元ファイルが無いのでバックアップなし
    ASSERT_TRUE (store.saveToFile (file).isEmpty());
    ASSERT_TRUE (store.saveToFile (file).isEmpty());

    const auto dir = eeq::core::ProjectStore::backupDirectoryFor (file);
    EXPECT_EQ (dir.findChildFiles (juce::File::findFiles, false, "project_*.eea").size(), 1); // 10 分以内は 1 個

    // 古いバックアップを上限より多く置いておくと、次のバックアップで上限まで減る
    for (int i = 0; i < eeq::core::ProjectStore::maxBackups + 5; ++i)
    {
        const auto old = dir.getChildFile ("project_2000010" + juce::String (i % 9 + 1) + "_0000" + juce::String (i).paddedLeft ('0', 2) + ".eea");
        old.replaceWithText ("{}");
        old.setLastModificationTime (juce::Time::getCurrentTime() - juce::RelativeTime::hours (5));
    }
    for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "project_2026*.eea"))
        f.setLastModificationTime (juce::Time::getCurrentTime() - juce::RelativeTime::hours (1));

    ASSERT_TRUE (store.saveToFile (file).isEmpty());
    const auto backups = dir.findChildFiles (juce::File::findFiles, false, "project_*.eea");
    EXPECT_EQ (backups.size(), eeq::core::ProjectStore::maxBackups);
    file.getParentDirectory().deleteRecursively();
}
