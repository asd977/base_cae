#ifndef THINFILMSETTINGWIDGET_H
#define THINFILMSETTINGWIDGET_H

#include <BasePage.h>
#include <QWidget>

namespace Ui {
class ThinFilmSettingWidget;
}

class ThinFilmSettingWidget : public BasePage
{
    Q_OBJECT


public:
    explicit ThinFilmSettingWidget(QWidget *parent = nullptr);
    ~ThinFilmSettingWidget();

    void beforeLoad() override;
    void beforeUnload() override;

private:
    Ui::ThinFilmSettingWidget *ui;
};

#endif // THINFILMSETTINGWIDGET_H
