#ifndef OUTPUTWIDGET_H
#define OUTPUTWIDGET_H

#include <QWidget>

namespace Ui {
class OutputWidget;
}

class OutputWidget : public QWidget
{
    Q_OBJECT

public:
    explicit OutputWidget(QWidget *parent = nullptr);
    ~OutputWidget();

    void outputMessage(QString &str);
    void showEvent(QShowEvent *event);
private:
    Ui::OutputWidget *ui;
    int MAX_LINES;
};

#endif // OUTPUTWIDGET_H
