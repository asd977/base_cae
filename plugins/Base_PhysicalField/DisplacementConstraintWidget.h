#ifndef DISPLACEMENTCONSTRAINTWIDGET_H
#define DISPLACEMENTCONSTRAINTWIDGET_H

#include <BasePage.h>
#include <QWidget>
#include <QVector>

namespace Ui {
class DisplacementConstraintWidget;
}

class DisplacementConstraintWidget : public BasePage
{
    Q_OBJECT

public:
    explicit DisplacementConstraintWidget(QWidget *parent = nullptr);
    ~DisplacementConstraintWidget();
    void beforeLoad() override;
    void beforeUnload() override;

private:
    void connectSignals();
    void refreshMeshInfo();
    void updateMatypeList();
    QVector<int> currentDofValues() const;
    QString defaultConstraintFilePath(const QString &fallbackName) const;
    void applyConstraintVisibility();

    Ui::DisplacementConstraintWidget *ui;
};

#endif // DISPLACEMENTCONSTRAINTWIDGET_H
