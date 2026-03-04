#ifndef MESHEDGECONTROLWIDGET_H
#define MESHEDGECONTROLWIDGET_H

#include <BasePage.h>
#include <services/MeshModuleControllerService.h>

#include <QMap>
#include <QStringList>
#include <QVector>

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

  private slots:
    void onAddControl();
    void onDeleteControl();
    void onResetForm();
    void onSelectionChanged();
    void onParameterEdited();

    void onAppendSelectedEntity();

  private:
    struct EdgeControlItem
    {
        QString edgeIds;
        int divisions = 10;
        double bias = 1.0;
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

  private:
    Ui::MeshEdgeControlWidget* ui;
    QVector<EdgeControlItem> m_items;
    QStringList m_entityIds;
    QString m_highlightPropId;
};

#endif // MESHEDGECONTROLWIDGET_H
