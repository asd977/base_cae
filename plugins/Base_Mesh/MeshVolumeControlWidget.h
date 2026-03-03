#ifndef MESHVOLUMECONTROLWIDGET_H
#define MESHVOLUMECONTROLWIDGET_H

#include <BasePage.h>
#include <QMap>
#include <QVector>
#include <QStringList>
#include <services/MeshModuleControllerService.h>
namespace Ui {
class MeshVolumeControlWidget;
}

class MeshVolumeControlWidget : public BasePage
{
    Q_OBJECT

public:
    explicit MeshVolumeControlWidget(QWidget *parent = nullptr);
    ~MeshVolumeControlWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

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
        double minSize = 0.5;
        double maxSize = 2.0;
        double growthRate = 1.2;
        bool boundaryLayer = false;
        int layerCount = 3;
    };

    void setupUiState();
    bool validateInput(QString &err, int ignoreRow = -1) const;
    VolumeControlItem collectFormData() const;
    void fillFormData(const VolumeControlItem &item);
    void appendOrUpdateTableRow(int row, const VolumeControlItem &item);
    void refreshButtons();
    void rebuildVolumeCandidates();
    void clearHighlight();
    void updateEntityUiState();
    void syncRulesToService();
    void loadRulesFromService();

private:
    Ui::MeshVolumeControlWidget *ui;
    QVector<VolumeControlItem> m_items;
    QStringList m_entityIds;
    QString m_highlightPropId;
};

#endif // MESHVOLUMECONTROLWIDGET_H
