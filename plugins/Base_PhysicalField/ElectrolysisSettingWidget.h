#ifndef ELECTROLYSISSETTINGWIDGET_H
#define ELECTROLYSISSETTINGWIDGET_H

#include <BasePage.h>
#include <QWidget>

namespace Ui {
class ElectrolysisSettingWidget;
}

class ElectrolysisSettingWidget : public BasePage
{
    Q_OBJECT

public:
    explicit ElectrolysisSettingWidget(QWidget *parent = nullptr);
    ~ElectrolysisSettingWidget();
    QWidget* getWidget() override { return this; }

    void beforeLoad() override;
    void beforeUnload() override;
private:
    Ui::ElectrolysisSettingWidget *ui;
};

#endif // ELECTROLYSISSETTINGWIDGET_H
