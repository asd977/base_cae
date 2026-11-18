#ifndef MESHGROUPINGWIDGET_H
#define MESHGROUPINGWIDGET_H

#include <BasePage.h>
#include <struct/MeshMap.h>
#include <vtkSmartPointer.h>

class vtkActor;
class QComboBox;
class QSpinBox;
class QLineEdit;
class QLabel;
class QTableWidget;
class QTabWidget;
class QPushButton;
class QGroupBox;
class QCheckBox;

namespace Ui
{
class MeshGroupingWidget;
}

class MeshGroupingWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit MeshGroupingWidget(QWidget* parent = nullptr);
    ~MeshGroupingWidget();

    void beforeLoad() override;
    void beforeUnload() override;

  private slots:
    void onAutoGrouping();
    void onRangeGrouping();
    void onRefreshGroups();
    void onToggleRangePreview(bool on);
    void onRangeValueChanged();
    void onAutoDetectBoundaries();

  private:
    void refreshGroupList();
    void refreshBoundaryGroupList();
    void detectBoundaryGroupsBySurfacePatches();
    void initializeRangeByGridBounds(bool force = false);
    void updateRangePreview(bool forceOn = false);
    void clearRangePreview(bool renderNow = true);
    QString currentTargetArrayName() const;
    void buildManualGroupingControls();
    void onPickedFacesChanged();
    void clearManualSelection();
    void selectGroupsFromTable();
    void updateGroupActionState();
    void mergeSelectedGroups();
    void syncGroupTab();
    void applyManualGrouping();
    void writeSelectionSet(const QString& action);
    void deleteSelectionSet();
    void refreshSelectionSets();
    void updateManualSelectionPreview();
    void updateManualCount();
    int selectedGroupId() const;
    QTableWidget* currentGroupTable() const;

  private:
    Ui::MeshGroupingWidget* ui;
    vtkSmartPointer<vtkActor> m_rangePreviewActor;
    bool m_hasAutoBoundaryGroups = false;
    MshPickMap m_autoBoundaryPickMap;
    QSet<MeshIdType> m_manualCellIds;
    QSet<qint64> m_lastFaceKeys;
    bool m_activeManualPicking = false;
    bool m_ignoreFaceSignal = false;
    QLabel* m_manualStatus = nullptr;
    QTableWidget* m_selectionSetsTable = nullptr;
    QGroupBox* m_groupActions = nullptr;
    QComboBox* m_selectMode = nullptr;
    QLabel* m_modeLabel = nullptr;
    QLabel* m_modeHint = nullptr;
    QLabel* m_targetLabel = nullptr;
    QPushButton* m_clearButton = nullptr;
    QPushButton* m_mergeButton = nullptr;
    QSpinBox* m_mergeTargetId = nullptr;
    QLineEdit* m_mergeTargetName = nullptr;
    bool m_advancedCellMode = false;
    bool m_rebuildingGroups = false;
    vtkSmartPointer<vtkActor> m_manualPreviewActor;
};

#endif // MESHGROUPINGWIDGET_H
