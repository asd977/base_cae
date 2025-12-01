#ifndef ELECTROPLATINGSETTINGWIDGET_H
#define ELECTROPLATINGSETTINGWIDGET_H

#include <BasePage.h>
#include <QWidget>

namespace Ui {
class ElectroPlatingSettingWidget;
}

class ElectroPlatingSettingWidget : public BasePage
{
    Q_OBJECT

public:
    explicit ElectroPlatingSettingWidget(QWidget *parent = nullptr);
    ~ElectroPlatingSettingWidget();
    void beforeLoad() override;
    void beforeUnload() override;

private:
    Ui::ElectroPlatingSettingWidget *ui;
};

#endif // ELECTROPLATINGSETTINGWIDGET_H
