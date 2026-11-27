#ifndef MESHEDGECONTROLWIDGET_H
#define MESHEDGECONTROLWIDGET_H

#include <BasePage.h>
#include <services/MeshModuleControllerService.h>
#include "core/MeshControlSelectionPresentation.h"

#include <QMap>
#include <QStringList>
#include <QVector>
#include <QEvent>
#include <QVariant>

class QPushButton;
class QLabel;

namespace Ui
{
class MeshEdgeControlWidget;
}

class MeshEdgeControlWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit MeshEdgeControlWidget(QWidget* parent = nullptr);
    ~MeshEdgeControlWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;
    void setOwnerTask(const QString& kind, const QString& uuid, const QString& displayName);

  protected:
    bool event(QEvent* ev) override;

  private slots:
    void onAddControl();
    void onDeleteControl();
    void onResetForm();
    void onSelectionChanged();
    void onParameterEdited();

    void onAppendSelectedEntity();
    bool appendCadEdge(int id);

  private:
    struct EdgeControlItem
    {
        QString edgeIds;
        QString partitionEdgeRefs;
        int divisions = 10;
        double bias = 1.0; // 旧工程兼容；新 UI 不再暴露增长率
    };

    void setupUiState();
    bool validateInput(QString& err, int ignoreRow = -1) const;
    EdgeControlItem collectFormData() const;
    void fillFormData(const EdgeControlItem& item);
    void appendOrUpdateTableRow(int row, const EdgeControlItem& item);
    void refreshButtons();
    void rebuildEdgeCandidates();
    void clearHighlight();
    void updateEntityUiState();
    void syncRulesToService();
    void loadRulesFromService();
    QObject* geometryBridgeObject() const;
    QVariantList partitionPlanes() const;
    void beginPartitionEdgePick();
    void finishPartitionEdgePick();
    void clearPartitionEdgeRefs();
    void showPartitionEdgeSelection();
    void updatePartitionRefLabel();
    void togglePartitionEdgeRef(int region, int edge);
    static QString normalizedPartitionRefs(const QString& text);

  private:
    Ui::MeshEdgeControlWidget* ui;
    QVector<EdgeControlItem> m_items;
    QStringList m_entityIds;
    QString m_highlightPropId;
    MeshControlSelectionPresentation m_selectionPresentation;
    QString m_ownerTaskKind;
    QString m_ownerTaskUuid;
    QString m_ownerTaskName;
    enum PartitionPickStage { PickPartitionNone = 0, PickCadEdge, PickPartitionRegion, PickPartitionEdge };
    PartitionPickStage m_partitionPickStage = PickPartitionNone;
    int m_partitionPickRegion = -1;
    QLabel* m_partitionPickCaption = nullptr;
    QPushButton* m_pickPartitionEdge = nullptr;
    QPushButton* m_clearPartitionEdges = nullptr;
    QLabel* m_partitionRefLabel = nullptr;
};

#endif // MESHEDGECONTROLWIDGET_H
