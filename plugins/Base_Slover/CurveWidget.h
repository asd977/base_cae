#ifndef CURVEWIDGET_H
#define CURVEWIDGET_H

#include <QWidget>

class Solver;
namespace Ui {
class CurveWidget;
}

class CurveWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CurveWidget(Solver *solver, QWidget *parent = nullptr);
    ~CurveWidget();

private slots:
    void on_testPushButton_clicked();

private:
    Ui::CurveWidget *ui;
    Solver *m_solver;
};

#endif // CURVEWIDGET_H
