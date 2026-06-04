#ifndef PHYSICALFIELDSETTINGWIDGET_H
#define PHYSICALFIELDSETTINGWIDGET_H

#include <BasePage.h>

#include <QWidget>

namespace Ui
{
class PhysicalFieldSettingWidget;
}

class PhysicalFieldSettingWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit PhysicalFieldSettingWidget(QWidget* parent = nullptr);
    ~PhysicalFieldSettingWidget();

    void beforeLoad() override;
    void beforeUnload() override;

  private slots:
    void on_importIIIDButton_clicked();

private:
    Ui::PhysicalFieldSettingWidget* ui;
};

#endif // PHYSICALFIELDSETTINGWIDGET_H
