#include "VenueTab.h"

#include "../Core/Log.h"
#include "Buttons.h"
#include "Strings.h"
#include "Theme.h"

namespace eeq::ui
{

namespace
{
class MicPointListModel : public juce::ListBoxModel
{
public:
    explicit MicPointListModel (VenueTab& ownerIn) : owner (ownerIn) {}

    int getNumRows() override { return owner.getNumMicPoints(); }

    void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool) override
    {
        g.fillAll (theme::get (theme::ColourId::panelBackground));
        g.setColour (theme::get (theme::ColourId::text));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText (owner.getMicPointText (rowNumber), 6, 0, width - 10, height, juce::Justification::centredLeft);
    }

private:
    VenueTab& owner;
};

class SessionListModel : public juce::ListBoxModel
{
public:
    explicit SessionListModel (VenueTab& ownerIn) : owner (ownerIn) {}

    int getNumRows() override { return owner.getNumSessions(); }

    void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool) override
    {
        g.fillAll (theme::get (theme::ColourId::panelBackground));
        g.setColour (theme::get (theme::ColourId::text));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText (owner.getSessionText (rowNumber), 6, 0, width - 10, height, juce::Justification::centredLeft);
    }

private:
    VenueTab& owner;
};
} // namespace

int VenueTab::getNumMicPoints() const
{
    return (int) ctx.project.currentVenue().micPoints.size();
}

juce::String VenueTab::getMicPointText (int row) const
{
    const auto& points = ctx.project.currentVenue().micPoints;

    if (row < 0 || row >= (int) points.size())
        return {};

    return points[(size_t) row].name + " - " + points[(size_t) row].description;
}

int VenueTab::getNumSessions() const
{
    return (int) ctx.project.currentVenue().sessions.size();
}

juce::String VenueTab::getSessionText (int row) const
{
    const auto& sessions = ctx.project.currentVenue().sessions;

    if (row < 0 || row >= (int) sessions.size())
        return {};

    return sessions[(size_t) row].name + "  ("
         + juce::String (sessions[(size_t) row].snapshots.size())
         + juce::String (u8" スナップショット)");
}

VenueTab::VenueTab (UiContext& context)
    : ctx (context)
{
    listTitle.setText (strings::venueSection, juce::dontSendNotification);
    styleLabel (listTitle, 13.0f, false);
    addAndMakeVisible (listTitle);

    searchTitle.setText (strings::venueSearchLabel, juce::dontSendNotification);
    styleLabel (searchTitle);
    addAndMakeVisible (searchTitle);

    styleTextEditor (searchEditor);
    searchEditor.onTextChange = [this] { refreshVenueList(); };
    addAndMakeVisible (searchEditor);

    venueList.setColour (juce::ListBox::backgroundColourId, theme::get (theme::ColourId::panelBackground));
    venueList.setRowHeight (22);
    addAndMakeVisible (venueList);

    styleButton (newVenueButton);
    newVenueButton.setButtonText (strings::venueNewButton);
    newVenueButton.onClick = [this]
    {
        eeq::core::Venue venue;
        venue.name = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M");
        venue.date = juce::Time::getCurrentTime();
        venue.temperatureC = ctx.state.airTemperatureC.load();
        ctx.project.venues().push_back (venue);
        ctx.state.snapshotVersion.fetch_add (1);
        refreshVenueList();
    };
    addAndMakeVisible (newVenueButton);

    // ---- 会場のフィールド ----
    auto makeField = [this] (juce::Label& label, juce::TextEditor& editor, const juce::String& title, bool multiline = false)
    {
        label.setText (title, juce::dontSendNotification);
        styleLabel (label);
        addAndMakeVisible (label);

        styleTextEditor (editor);

        if (multiline)
            editor.setMultiLine (true, true);

        editor.onTextChange = [this] { saveFieldsToVenue(); };
        addAndMakeVisible (editor);
    };

    makeField (nameLabel, nameEditor, strings::venueNameLabel);
    makeField (tempLabel2, tempEditor, strings::venueTempLabel);
    makeField (humidityLabel, humidityEditor, strings::venueHumidityLabel);
    makeField (crossoverLabel, crossoverEditor, juce::String (u8"サブのクロスオーバー周波数 (Hz。0=未入力)"));
    makeField (equipmentLabel, equipmentEditor, strings::venueEquipmentLabel);
    makeField (notesLabel, notesEditor, strings::venueNotesLabel, true);

    tempEditor.setInputRestrictions (8, "0123456789.-");
    humidityEditor.setInputRestrictions (6, "0123456789.");
    crossoverEditor.setInputRestrictions (8, "0123456789.");

    // ---- マイクポイント ----
    micPointsTitle.setText (strings::venueMicPoints, juce::dontSendNotification);
    styleLabel (micPointsTitle, 12.5f, false);
    addAndMakeVisible (micPointsTitle);

    micPointList.setColour (juce::ListBox::backgroundColourId, theme::get (theme::ColourId::panelBackground));
    micPointList.setRowHeight (20);
    addAndMakeVisible (micPointList);

    styleTextEditor (micPointNameEditor);
    styleTextEditor (micPointDescEditor);
    micPointNameEditor.setTextToShowWhenEmpty (strings::venueMicPointName, theme::get (theme::ColourId::textDim));
    micPointDescEditor.setTextToShowWhenEmpty (strings::venueMicPointDesc, theme::get (theme::ColourId::textDim));
    addAndMakeVisible (micPointNameEditor);
    addAndMakeVisible (micPointDescEditor);

    styleButton (micPointAddButton);
    micPointAddButton.setButtonText (strings::addButton);
    micPointAddButton.onClick = [this]
    {
        const auto name = micPointNameEditor.getText().trim();

        if (name.isEmpty())
            return;

        eeq::core::MicPoint point;
        point.name = name;
        point.description = micPointDescEditor.getText().trim();
        ctx.project.currentVenue().micPoints.push_back (point);
        micPointNameEditor.clear();
        micPointDescEditor.clear();
        micPointList.updateContent();
    };
    addAndMakeVisible (micPointAddButton);

    styleButton (micPointRemoveButton);
    micPointRemoveButton.setButtonText (strings::removeButton);
    micPointRemoveButton.onClick = [this]
    {
        auto& points = ctx.project.currentVenue().micPoints;
        const auto selected = micPointList.getSelectedRow();

        if (selected >= 0 && selected < (int) points.size())
        {
            points.erase (points.begin() + selected);
            micPointList.updateContent();
        }
    };
    addAndMakeVisible (micPointRemoveButton);

    // ---- セッション ----
    sessionsTitle.setText (strings::venueSessions, juce::dontSendNotification);
    styleLabel (sessionsTitle, 12.5f, false);
    addAndMakeVisible (sessionsTitle);

    sessionList.setColour (juce::ListBox::backgroundColourId, theme::get (theme::ColourId::panelBackground));
    sessionList.setRowHeight (20);
    addAndMakeVisible (sessionList);

    // ---- プロジェクト入出力 ----
    styleButton (saveProjectButton);
    saveProjectButton.setButtonText (strings::venueSaveProject);
    saveProjectButton.onClick = [this] { saveProject(); };
    addAndMakeVisible (saveProjectButton);

    styleButton (loadProjectButton);
    loadProjectButton.setButtonText (strings::venueLoadProject);
    loadProjectButton.onClick = [this] { loadProject(); };
    addAndMakeVisible (loadProjectButton);

    styleButton (reportButton);
    reportButton.setButtonText (strings::venueReportButton);
    reportButton.onClick = [this] { exportReportPng(); };
    addAndMakeVisible (reportButton);

    statusLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::textDim));
    addAndMakeVisible (statusLabel);

    micPointModel = std::make_unique<MicPointListModel> (*this);
    sessionModel = std::make_unique<SessionListModel> (*this);
    micPointList.setModel (micPointModel.get());
    sessionList.setModel (sessionModel.get());

    refreshVenueList();
    loadSelectedVenueFields();
}

void VenueTab::timerTick()
{
    // プロジェクトの復元/読込などでスナップショットのバージョンが進んだら一覧を張り直す
    const auto version = (int) ctx.state.snapshotVersion.load();

    if (version != seenSnapshotVersion)
    {
        seenSnapshotVersion = version;
        refreshVenueList();
        loadSelectedVenueFields();
    }
}

void VenueTab::refreshVenueList()
{
    const auto query = searchEditor.getText();
    const auto matches = ctx.project.findVenues (query);
    const auto& venues = ctx.project.venues();

    juce::Array<int> visibleVenues;

    for (const auto index : matches)
    {
        // 表示中の会場だけを行として扱うため、行番号→実インデックスの対応を作る
        visibleVenues.add (index);
    }

    venueIndices.clear();

    for (const auto index : matches)
        venueIndices.push_back (index);
    venueList.updateContent();
    micPointList.updateContent();
    sessionList.updateContent();
}

void VenueTab::loadSelectedVenueFields()
{
    const auto& venue = ctx.project.currentVenue();
    nameEditor.setText (venue.name, juce::dontSendNotification);
    tempEditor.setText (juce::String (venue.temperatureC, 1), juce::dontSendNotification);
    humidityEditor.setText (juce::String (venue.humidityPercent, 0), juce::dontSendNotification);
    crossoverEditor.setText (venue.subCrossoverHz > 0.0 ? juce::String (venue.subCrossoverHz, 0) : "0",
                             juce::dontSendNotification);
    equipmentEditor.setText (venue.equipment, juce::dontSendNotification);
    notesEditor.setText (venue.notes, juce::dontSendNotification);
}

void VenueTab::saveFieldsToVenue()
{
    auto& venue = ctx.project.currentVenue();

    if (nameEditor.getText().trim().isNotEmpty())
        venue.name = nameEditor.getText().trim();

    const auto temp = tempEditor.getText().getDoubleValue();

    if (temp > -20.0 && temp < 60.0)
    {
        venue.temperatureC = temp;
        ctx.state.airTemperatureC.store (temp); // 気温は測定の換算にも使う（仕様 3.3.2）
    }

    venue.humidityPercent = humidityEditor.getText().getDoubleValue();
    venue.subCrossoverHz = crossoverEditor.getText().getDoubleValue(); // 0 = 未入力
    venue.equipment = equipmentEditor.getText();
    venue.notes = notesEditor.getText();
    venue.date = juce::Time::getCurrentTime();
}

void VenueTab::saveProject()
{
    saveFieldsToVenue();

    auto chooser = std::make_shared<juce::FileChooser> ("project.eea",
                                                        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                        "*.eea");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<VenueTab> (this), chooser] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();

            if (file == juce::File() || safeThis == nullptr)
                return;

            const auto error = safeThis->ctx.project.saveToFile (file);
            safeThis->statusLabel.setText (error.isEmpty() ? strings::venueSaveOk : strings::venueSaveFailed + ": " + error,
                                           juce::dontSendNotification);
            eeq::log::write (error.isEmpty() ? "project saved: " + file.getFullPathName()
                                             : "project save FAILED: " + error);
        });
}

void VenueTab::loadProject()
{
    auto chooser = std::make_shared<juce::FileChooser> (strings::fileChooserOpen,
                                                        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                        "*.eea");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<VenueTab> (this), chooser] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();

            if (file == juce::File() || safeThis == nullptr)
                return;

            const auto error = safeThis->ctx.project.loadFromFile (file);
            safeThis->statusLabel.setText (error.isEmpty() ? strings::venueLoadOk : strings::venueLoadFailed + ": " + error,
                                           juce::dontSendNotification);
            safeThis->ctx.state.snapshotVersion.fetch_add (1);
            safeThis->refreshVenueList();
            safeThis->loadSelectedVenueFields();
        });
}

void VenueTab::exportReportPng()
{
    // 会場情報・カーブ・遅延値・補正EQ案を1枚にまとめたPNGレポート（仕様 6.2 / 6.4、書き出しは任意）
    saveFieldsToVenue();
    const auto& venue = ctx.project.currentVenue();

    const int width = 900, height = 640;
    juce::Image image (juce::Image::ARGB, width, height, true);
    juce::Graphics g (image);

    g.fillAll (juce::Colours::white);
    g.setColour (juce::Colours::black);

    int y = 30;
    g.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    g.drawText (venue.name, 30, y, width - 60, 30, juce::Justification::centredLeft);
    y += 34;

    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawText (venue.date.toString (true, true, false, true), 30, y, width - 60, 18, juce::Justification::centredLeft);
    y += 24;
    g.drawText (juce::String (u8"気温: ") + juce::String (venue.temperatureC, 1)
                    + juce::String (u8"℃  湿度: ") + juce::String (venue.humidityPercent, 0) + "%",
                30, y, width - 60, 18, juce::Justification::centredLeft);
    y += 24;

    juce::String systemsText;

    for (const auto& system : venue.systems)
        systemsText += (systemsText.isEmpty() ? "" : ", ") + system.name + " (ch" + juce::String (system.outputChannel + 1) + ")";

    g.drawText (juce::String (u8"スピーカー構成: ") + (systemsText.isEmpty() ? u8"未登録" : systemsText),
                30, y, width - 60, 18, juce::Justification::centredLeft);
    y += 24;
    g.drawText (juce::String (u8"サブのクロスオーバー: ")
                    + (venue.subCrossoverHz > 0.0 ? juce::String (venue.subCrossoverHz, 0) + " Hz" : juce::String (u8"未入力")),
                30, y, width - 60, 18, juce::Justification::centredLeft);
    y += 26;

    if (venue.notes.isNotEmpty())
    {
        g.drawText (juce::String (u8"メモ: "), 30, y, width - 60, 16, juce::Justification::centredLeft);
        y += 18;
        g.drawMultiLineText (venue.notes, 40, y + 12, width - 80);
        y += 60;
    }

    // 補正EQ案（仕様 6.4）
    g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    g.drawText (juce::String (u8"補正EQ案"), 30, y, width - 60, 20, juce::Justification::centredLeft);
    y += 24;
    g.setFont (juce::Font (juce::FontOptions (12.0f)));

    int index = 1;

    for (const auto& band : ctx.state.proposedBands)
    {
        g.drawText (juce::String (index) + ": " + juce::String (band.frequency, 1) + " Hz  "
                        + (band.gainDb > 0 ? "+" : "") + juce::String (band.gainDb, 2) + " dB  Q "
                        + juce::String (band.q, 2),
                    40, y, width - 80, 16, juce::Justification::centredLeft);
        y += 18;
        ++index;
    }

    if (ctx.state.proposedBands.empty())
        g.drawText (juce::String (u8"（帯域なし）"), 40, y, width - 80, 16, juce::Justification::centredLeft);

    auto chooser = std::make_shared<juce::FileChooser> ("report.png",
                                                        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                        "*.png");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [chooser, image] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();

            if (file == juce::File())
                return;

            juce::PNGImageFormat png;
            juce::FileOutputStream stream (file);
            png.writeImageToStream (image, stream);
        });
}

int VenueTab::getNumRows()
{
    return venueIndices.size();
}

void VenueTab::paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    const auto& venues = ctx.project.venues();

    if (rowNumber < 0 || rowNumber >= venueIndices.size())
        return;

    const auto& venue = venues[(size_t) venueIndices[(size_t) rowNumber]];

    g.fillAll (rowIsSelected ? theme::get (theme::ColourId::control)
                             : theme::get (theme::ColourId::panelBackground));
    g.setColour (theme::get (theme::ColourId::text));
    g.setFont (juce::Font (juce::FontOptions (12.5f)));
    g.drawText (venue.name, 6, 0, width - 12, height, juce::Justification::centredLeft);
}

void VenueTab::listBoxItemClicked (int rowNumber, const juce::MouseEvent&)
{
    if (rowNumber < 0 || rowNumber >= venueIndices.size())
        return;

    selectedVenueIndex = venueIndices[(size_t) rowNumber];
    ctx.project.setCurrentVenueIndex (selectedVenueIndex);

    // 会場が変わるとスナップショット一覧（vector）も入れ替わる。
    // 他タブが保持する生ポインタを張り替えさせるためバージョンを進める
    ctx.state.snapshotVersion.fetch_add (1);

    loadSelectedVenueFields();
    micPointList.updateContent();
    sessionList.updateContent();
}

void VenueTab::paint (juce::Graphics& g)
{
    g.fillAll (theme::get (theme::ColourId::windowBackground));
}

void VenueTab::resized()
{
    auto bounds = getLocalBounds().reduced (8);

    // 左: 会場一覧
    auto left = bounds.removeFromLeft (bounds.getWidth() * 2 / 7);
    listTitle.setBounds (left.removeFromTop (18));
    auto searchRow = left.removeFromTop (26);
    searchTitle.setBounds (searchRow.removeFromLeft (46));
    searchEditor.setBounds (searchRow);
    left.removeFromTop (4);
    venueList.setBounds (left.removeFromTop (200));
    left.removeFromTop (4);
    newVenueButton.setBounds (left.removeFromTop (26).removeFromLeft (110));

    // 中: 会場のフィールド
    auto middle = bounds.removeFromLeft (bounds.getWidth() / 2);
    auto field = [&middle] (juce::Label& label, juce::TextEditor& editor, int height)
    {
        label.setBounds (middle.removeFromTop (16));
        middle.removeFromTop (2);
        editor.setBounds (middle.removeFromTop (height));
        middle.removeFromTop (4);
    };

    field (nameLabel, nameEditor, 24);

    auto tripleRow = middle.removeFromTop (24);
    tempLabel2.setBounds (tripleRow.removeFromLeft (80));
    tempEditor.setBounds (tripleRow.removeFromLeft (60));
    tripleRow.removeFromLeft (8);
    humidityLabel.setBounds (tripleRow.removeFromLeft (70));
    humidityEditor.setBounds (tripleRow.removeFromLeft (50));

    // クロスオーバーは独立したフィールドにする（同じ行に詰めると中カラム幅に
    // 収まらず、入力欄の幅が 0 になって入力できなくなる）
    middle.removeFromTop (4);
    field (crossoverLabel, crossoverEditor, 24);
    field (equipmentLabel, equipmentEditor, 24);
    field (notesLabel, notesEditor, std::max (40, middle.getHeight() - 10));

    // 右: マイクポイント / セッション / 保存
    auto right = bounds;
    micPointsTitle.setBounds (right.removeFromTop (18));
    micPointList.setBounds (right.removeFromTop (110));
    right.removeFromTop (4);
    auto micRow = right.removeFromTop (24);
    micPointNameEditor.setBounds (micRow.removeFromLeft (100));
    micRow.removeFromLeft (4);
    micPointDescEditor.setBounds (micRow.removeFromLeft (130));
    micRow.removeFromLeft (4);
    micPointAddButton.setBounds (micRow.removeFromLeft (60));
    micRow.removeFromLeft (4);
    micPointRemoveButton.setBounds (micRow.removeFromLeft (60));

    right.removeFromTop (8);
    sessionsTitle.setBounds (right.removeFromTop (18));
    sessionList.setBounds (right.removeFromTop (110));

    right.removeFromTop (8);
    auto saveRow = right.removeFromTop (26);
    saveProjectButton.setBounds (saveRow.removeFromLeft (170));
    saveRow.removeFromLeft (6);
    loadProjectButton.setBounds (saveRow.removeFromLeft (150));
    right.removeFromTop (4);
    // レポートボタンは独立行にする（同じ行だと右カラム幅に収まらず潰れる）
    reportButton.setBounds (right.removeFromTop (26).removeFromLeft (180));
    statusLabel.setBounds (right.removeFromTop (18));
}

} // namespace eeq::ui
