#pragma once

#include "Core/JuceModules.h"

#include "UiContext.h"

namespace eeq::ui
{

// 「会場」タブ（仕様 6.2 / 6.4）。
// 会場ごとに、会場名・日付・気温・湿度・メモ・スピーカー構成（系統）・使用機材を保存する。
// マイクポイントを登録し、スナップショットと SPLログに紐づける。
// 会場→測定セッション→スナップショットの階層を .eea ファイルとして保存/読込する。
class VenueTab : public juce::Component,
                 private juce::ListBoxModel
{
public:
    explicit VenueTab (UiContext& context);

    void timerTick();
    void paint (juce::Graphics&) override;
    void resized() override;

    /** リストモデル用のアクセサ（cpp 内の ListBoxModel から使う） */
    int getNumMicPoints() const;
    juce::String getMicPointText (int row) const;
    int getNumSessions() const;
    juce::String getSessionText (int row) const;

private:
    // ListBoxModel（会場一覧）
    int getNumRows() override;
    void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked (int rowNumber, const juce::MouseEvent&) override;

    void refreshVenueList();
    void loadSelectedVenueFields();
    void saveFieldsToVenue();
    void saveProject();
    void loadProject();
    void exportReportPng();

    UiContext& ctx;

    juce::Label listTitle, searchTitle, fieldsTitle, micPointsTitle, sessionsTitle;
    juce::TextEditor searchEditor;
    juce::ListBox venueList { {}, this };
    juce::TextButton newVenueButton;

    juce::Label nameLabel, tempLabel2, humidityLabel, crossoverLabel, equipmentLabel, notesLabel;
    juce::TextEditor nameEditor, tempEditor, humidityEditor, crossoverEditor, equipmentEditor, notesEditor;

    juce::ListBox micPointList { {}, nullptr };
    std::unique_ptr<juce::ListBoxModel> micPointModel;
    juce::TextEditor micPointNameEditor, micPointDescEditor;
    juce::TextButton micPointAddButton, micPointRemoveButton;

    juce::Label sessionListLabel;
    juce::ListBox sessionList { {}, nullptr };
    std::unique_ptr<juce::ListBoxModel> sessionModel;

    juce::TextButton saveProjectButton, loadProjectButton, reportButton;
    juce::Label statusLabel;

    int selectedVenueIndex = 0;
    int seenSnapshotVersion = -1;
    std::vector<int> venueIndices;
};

} // namespace eeq::ui
