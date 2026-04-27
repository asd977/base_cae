#pragma once

#include <BasePage.h>
#include <PickManager.h>

#include <QSet>
#include <QString>
#include <vtkType.h>

class QComboBox;
class QPushButton;
class QListWidget;

class CaveExcavationBodyPage : public BasePage
{
    Q_OBJECT
  public:
    explicit CaveExcavationBodyPage(QWidget* parent = nullptr);

    void beforeLoad() override;
    void beforeUnload() override;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

    QVector<int> selectedElementIds();

  private slots:
    void onClearClicked();
    void onAddGroupClicked();
    void onRemoveSelectedGroupsClicked();
    void onGroupHovered(int index);
    void onGroupDropdownClosed();

  private:
    void setupGroupCombo();
    QVector<int> availableGroupIds() const;
    QVector<vtkIdType> groupCellIds(int groupId) const;
    QVector<vtkIdType> visibleBoundaryFaceKeys(const QVector<vtkIdType>& cellIds) const;
    QVector<int> elementIdsFromCells(const QVector<vtkIdType>& cellIds) const;
    void refreshSelectedGroupList();
    void refreshGroupComboMarks();
    void updatePersistentSelectionFaces();
    void updateGroupDomainHighlight(int previewGroup = -1);
    QSet<int> selectedLayerGroups() const;
    PickMode currentPickMode() const;

  private:
    int m_lastHoveredIndex = -1;

    QComboBox* m_groupCombo = nullptr;
    QPushButton* m_addGroupBtn = nullptr;
    QPushButton* m_removeGroupBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
    QListWidget* m_groupList = nullptr;
};
