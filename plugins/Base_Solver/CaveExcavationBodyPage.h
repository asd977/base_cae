#pragma once

#include <BasePage.h>
#include <PickManager.h>

#include <QSet>
#include <QString>
#include <vtkType.h>

class QComboBox;
class QPushButton;
class QListWidget;
class QTimer;
namespace Ui
{
class CaveExcavationBodyPage;
}

class CaveExcavationBodyPage : public BasePage
{
    Q_OBJECT
  public:
    explicit CaveExcavationBodyPage(QWidget* parent = nullptr);
    ~CaveExcavationBodyPage() override;

    void beforeLoad() override;
    void beforeUnload() override;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

    int excavationLayer() const;
    void setExcavationLayer(int layer);
    QVector<int> selectedElementIds();

  signals:
    void excavationLayerChanged(int layer);

  private slots:
    void onClearClicked();
    void onAddGroupClicked();
    void onRemoveSelectedGroupsClicked();
    void onGroupChanged(int index);
    void onGroupHovered(int index);
    void onGroupDropdownClosed();

  private:
    void setupGroupCombo();
    QVector<int> availableGroupIds() const;
    QString groupDisplayText(int groupId) const;
    QVector<vtkIdType> groupCellIds(int groupId) const;
    QVector<vtkIdType> visibleBoundaryFaceKeys(const QVector<vtkIdType>& cellIds) const;
    QVector<int> elementIdsFromCells(const QVector<vtkIdType>& cellIds) const;
    void refreshSelectedGroupList();
    void refreshGroupComboMarks();
    void updatePersistentSelectionFaces();
    void updateGroupDomainHighlight(int previewGroup = -1);
    QSet<int> selectedLayerGroups();
    QString selectionGroupCategory() const;
    PickMode currentPickMode() const;
    int layerDomainDim() const;

  private:
    Ui::CaveExcavationBodyPage* ui = nullptr;

    int m_lastHoveredIndex = -1;
    int m_pendingPreviewGroup = 0;

    QComboBox* m_groupCombo = nullptr;
    QPushButton* m_addGroupBtn = nullptr;
    QPushButton* m_removeGroupBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
    QListWidget* m_groupList = nullptr;
    QTimer* m_groupPreviewTimer = nullptr;
};
