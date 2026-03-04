#ifndef SETTINGWIDGET_H
#define SETTINGWIDGET_H

#include <qstackedwidget.h>

#include <QHBoxLayout>
#include <QWidget>

namespace Ui
{
class SettingWidget;
}

class SettingWidget : public QWidget
{
    Q_OBJECT

  public:
    explicit SettingWidget(QWidget* parent = nullptr);
    ~SettingWidget();
    QStackedWidget* getStackedWidget() const
    {
        return m_stackedWidget;
    };

  private:
    Ui::SettingWidget* ui;
    QHBoxLayout* m_layout;
    QStackedWidget* m_stackedWidget;
};

#endif // SETTINGWIDGET_H
