#ifndef PREPROCESSINGWIDGET_H
#define PREPROCESSINGWIDGET_H

#include <QWidget>

namespace Ui {
class PreProcessingWidget;
}

class PreProcessingWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PreProcessingWidget(QWidget *parent = nullptr);
    ~PreProcessingWidget();

private:
    Ui::PreProcessingWidget *ui;
};

#endif // PREPROCESSINGWIDGET_H
