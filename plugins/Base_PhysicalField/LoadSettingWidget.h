#ifndef LOADSETTINGWIDGET_H
#define LOADSETTINGWIDGET_H

#include <BasePage.h>
#include <QWidget>

namespace Ui {
class LoadSettingWidget;
}

class LoadSettingWidget : public BasePage
{
    Q_OBJECT

public:
    explicit LoadSettingWidget(QWidget *parent = nullptr);
    ~LoadSettingWidget();

    void beforeLoad() override;
    void beforeUnload() override;

private:
    Ui::LoadSettingWidget *ui;
};

#endif // LOADSETTINGWIDGET_H
