#ifndef ELECTRODESETTINGWIDGET_H
#define ELECTRODESETTINGWIDGET_H

#include <BasePage.h>
#include <QWidget>

namespace Ui {
class ElectrodeSettingWidget;
}

class ElectrodeSettingWidget : public BasePage
{
    Q_OBJECT

public:
    explicit ElectrodeSettingWidget(QWidget *parent = nullptr);
    ~ElectrodeSettingWidget();

    void beforeLoad() override;
    void beforeUnload() override;

private:
    Ui::ElectrodeSettingWidget *ui;
};

#endif // ELECTRODESETTINGWIDGET_H
