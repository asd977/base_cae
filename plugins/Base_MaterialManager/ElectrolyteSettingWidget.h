#ifndef ELECTROLYTESETTINGWIDGET_H
#define ELECTROLYTESETTINGWIDGET_H

#include <BasePage.h>
#include <QWidget>

namespace Ui {
class ElectrolyteSettingWidget;
}

class ElectrolyteSettingWidget : public BasePage
{
    Q_OBJECT

public:
    explicit ElectrolyteSettingWidget(QWidget *parent = nullptr);
    ~ElectrolyteSettingWidget();

    void beforeLoad() override;
    void beforeUnload() override;

private:
    Ui::ElectrolyteSettingWidget *ui;
};

#endif // ELECTROLYTESETTINGWIDGET_H
