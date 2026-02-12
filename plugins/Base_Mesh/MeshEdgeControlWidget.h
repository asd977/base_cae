#ifndef MESHEDGECONTROLWIDGET_H
#define MESHEDGECONTROLWIDGET_H

#include <BasePage.h>
#include <QVector>

class QTableWidgetItem;

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

private:
    Ui::MeshEdgeControlWidget *ui;
    QVector<EdgeControlItem> m_items;
};

#endif // MESHEDGECONTROLWIDGET_H
