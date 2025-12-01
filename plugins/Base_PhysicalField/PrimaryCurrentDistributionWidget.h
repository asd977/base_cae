#ifndef PRIMARYCURRENTDISTRIBUTIONWIDGET_H
#define PRIMARYCURRENTDISTRIBUTIONWIDGET_H

#include <BasePage.h>
#include <ProjectNode.h>
#include <QTreeWidgetItem>
#include <QWidget>

namespace Ui {
class PrimaryCurrentDistributionWidget;
}

class PrimaryCurrentDistributionWidget : public BasePage
{
    Q_OBJECT

public:
    explicit PrimaryCurrentDistributionWidget(QWidget *parent = nullptr);
    ~PrimaryCurrentDistributionWidget();

    void beforeLoad() override;

    void beforeUnload() override;

    void setNode(ProjectNode *node);

private slots:
    void on_electrolytePushButton_clicked();

    void on_electrodeSurfacePushButton_clicked();

    void on_insulationPushButton_clicked();

private:
    Ui::PrimaryCurrentDistributionWidget *ui;

    ProjectNode *m_node;
};

#endif // PRIMARYCURRENTDISTRIBUTIONWIDGET_H
