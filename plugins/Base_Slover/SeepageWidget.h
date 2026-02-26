#ifndef SEEPAGEWIDGET_H
#define SEEPAGEWIDGET_H

#include <QWidget>

namespace Ui {
class SeepageWidget;
}

class SeepageWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SeepageWidget(QWidget *parent = nullptr);
    ~SeepageWidget();

private:
    Ui::SeepageWidget *ui;
};

#endif // SEEPAGEWIDGET_H
