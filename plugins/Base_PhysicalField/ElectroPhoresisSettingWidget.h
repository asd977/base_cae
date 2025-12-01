#ifndef ELECTROPHORESISSETTINGWIDGET_H
#define ELECTROPHORESISSETTINGWIDGET_H

#include <BasePage.h>
#include <QWidget>

namespace Ui {
class ElectroPhoresisSettingWidget;
}

class ElectroPhoresisSettingWidget : public BasePage
{
    Q_OBJECT

public:
    explicit ElectroPhoresisSettingWidget(QWidget *parent = nullptr);
    ~ElectroPhoresisSettingWidget();

    void beforeLoad() override;
    void beforeUnload() override;
private:
    Ui::ElectroPhoresisSettingWidget *ui;
};

#endif // ELECTROPHORESISSETTINGWIDGET_H
