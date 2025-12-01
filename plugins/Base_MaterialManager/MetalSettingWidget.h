#ifndef METALSETTINGWIDGET_H
#define METALSETTINGWIDGET_H

#include <BasePage.h>
#include <QWidget>
namespace Ui {
class MetalSettingWidget;
}

class MetalSettingWidget : public BasePage
{
    Q_OBJECT

public:
    explicit MetalSettingWidget(QWidget *parent = nullptr);
    ~MetalSettingWidget();
    void beforeLoad() override;
    void beforeUnload() override;
private:
    Ui::MetalSettingWidget *ui;
};

#endif // METALSETTINGWIDGET_H
