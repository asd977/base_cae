#ifndef CURVEWIDGET_H
#define CURVEWIDGET_H

#include <QWidget>

namespace Ui {
class CurveWidget;
}

class CurveWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CurveWidget(QWidget *parent = nullptr);
    ~CurveWidget();

private:
    Ui::CurveWidget *ui;
};

#endif // CURVEWIDGET_H
