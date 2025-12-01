#ifndef POSTPROCESSINGWIDGET_H
#define POSTPROCESSINGWIDGET_H

#include <QWidget>

namespace Ui {
class PostProcessingWidget;
}

class PostProcessingWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PostProcessingWidget(QWidget *parent = nullptr);
    ~PostProcessingWidget();

private:
    Ui::PostProcessingWidget *ui;
};

#endif // POSTPROCESSINGWIDGET_H
