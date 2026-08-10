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
class CaveCraneBeamAreaPage;
}

class CaveCraneBeamAreaPage : public BasePage
{
    Q_OBJECT

  public:
    explicit CaveCraneBeamAreaPage(QWidget* parent = nullptr);
    ~CaveCraneBeamAreaPage() override;

    void beforeLoad() override;
    void beforeUnload() override;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

    QVector<int> selectedElementIds();
    QVector<int> selectedMaterialIds(QString* errorMessage = nullptr);
    bool exportMaterialSet(const QString& inputDirectory, QString* errorMessage = nullptr);
    int selectionCount();

  signals:
    void selectionChanged(int count);

  private slots:
    void onClearClicked();
    void onAddGroupClicked();
    void onRemoveSelectedGroupsClicked();
    void onGroupChanged(int index);
    void onGroupHovered(int index);
    void onGroupDropdownClosed();

  private:
    QString pickDomain() const;
    void setupGroupCombo();
    QVector<int> availableGroupIds();
    QString groupDisplayText(int groupId);
    QVector<vtkIdType> groupCellIds(int groupId);
    QVector<int> elementIdsFromCells(const QVector<vtkIdType>& cellIds);
    void refreshSelectedGroupList();
    void refreshGroupComboMarks();
    void updateGroupDomainHighlight(int previewGroup = 0);
    QSet<int> selectedGroups();
    QString selectionGroupCategory();
    int domainDimension();

  private:
    Ui::CaveCraneBeamAreaPage* ui = nullptr;
    int m_lastHoveredIndex = -1;
    int m_pendingPreviewGroup = 0;
    QComboBox* m_groupCombo = nullptr;
    QPushButton* m_addGroupButton = nullptr;
    QPushButton* m_removeGroupButton = nullptr;
    QPushButton* m_clearButton = nullptr;
    QListWidget* m_groupList = nullptr;
    QTimer* m_groupPreviewTimer = nullptr;
};
