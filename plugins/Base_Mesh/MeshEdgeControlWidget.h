#ifndef MESHEDGECONTROLWIDGET_H
#define MESHEDGECONTROLWIDGET_H

#include <BasePage.h>
#include <QMap>
#include <QVector>
#include <QStringList>

namespace Ui {
class MeshEdgeControlWidget;
}

class MeshEdgeControlWidget : public BasePage
{
    Q_OBJECT

public:
    explicit MeshEdgeControlWidget(QWidget *parent = nullptr);
    ~MeshEdgeControlWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

private slots:
    void onAddControl();
    void onUpdateControl();
    void onDeleteControl();
    void onResetForm();
    void onSelectionChanged();
    void onParameterEdited();

    void onRefreshEntities();
    void onAppendSelectedEntity();
    void onHighlightSelectedEntity();

private:
    struct EdgeControlItem
    {
        QString edgeIds;
        double size = 1.0;
        int divisions = 10;
        double bias = 1.0;
        bool hardConstraint = false;
    };

    void setupUiState();
    bool validateInput(QString &err) const;
    EdgeControlItem collectFormData() const;
    void fillFormData(const EdgeControlItem &item);
    void appendOrUpdateTableRow(int row, const EdgeControlItem &item);
    void refreshButtons();
    void refreshSummary();
    void rebuildEdgeCandidates();
    void clearHighlight();
    void updateEntityUiState();

private:
    Ui::MeshEdgeControlWidget *ui;
    QVector<EdgeControlItem> m_items;
    QStringList m_entityIds;
    QString m_highlightPropId;
};

#endif // MESHEDGECONTROLWIDGET_H
