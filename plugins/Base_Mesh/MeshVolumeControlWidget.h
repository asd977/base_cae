#ifndef MESHVOLUMECONTROLWIDGET_H
#define MESHVOLUMECONTROLWIDGET_H

#include <BasePage.h>
#include <services/MeshModuleControllerService.h>
#include "core/MeshControlSelectionPresentation.h"

#include <QMap>
#include <QStringList>
#include <QVector>
class QComboBox;
class QLabel;
namespace Ui
{
class MeshVolumeControlWidget;
}

class MeshVolumeControlWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit MeshVolumeControlWidget(QWidget* parent = nullptr);
    ~MeshVolumeControlWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;
    void setOwnerTask(const QString& kind, const QString& uuid, const QString& displayName);

  private slots:
    void onAddControl();
    void onDeleteControl();
    void onResetForm();
    void onSelectionChanged();
    void onParameterEdited();

    void onAppendSelectedEntity();

  private:
    struct VolumeControlItem
    {
        QString volumeIds;
        double localSizeFactor = 1.0;
    };

    void setupUiState();
    bool validateInput(QString& err, int ignoreRow = -1) const;
    VolumeControlItem collectFormData() const;
    void fillFormData(const VolumeControlItem& item);
    void appendOrUpdateTableRow(int row, const VolumeControlItem& item);
    void refreshButtons();
    void rebuildVolumeCandidates();
    void clearHighlight();
    void updateEntityUiState();
    void syncRulesToService();
    void loadRulesFromService();
    void refreshSizePreview();
    QString domainNames(const QString& ids) const;

  private:
    Ui::MeshVolumeControlWidget* ui;
    QVector<VolumeControlItem> m_items;
    QStringList m_entityIds;
    QString m_highlightPropId;
    MeshControlSelectionPresentation m_selectionPresentation;
    QString m_ownerTaskKind;
    QString m_ownerTaskUuid;
    QString m_ownerTaskName;
    QMap<int, QString> m_domainNames;
    QComboBox* m_densityPreset = nullptr;
    QLabel* m_sizePreview = nullptr;
    QLabel* m_ruleStatus = nullptr;
    void updateRuleStatus(const QString& message, bool invalid = false);
};

#endif // MESHVOLUMECONTROLWIDGET_H
