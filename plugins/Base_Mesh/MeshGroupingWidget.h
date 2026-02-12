#ifndef MESHGROUPINGWIDGET_H
#define MESHGROUPINGWIDGET_H

#include <QWidget>

namespace Ui {
class MeshGroupingWidget;
}

class MeshGroupingWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MeshGroupingWidget(QWidget *parent = nullptr);
    ~MeshGroupingWidget();

private:
    Ui::MeshGroupingWidget *ui;
};

#endif // MESHGROUPINGWIDGET_H
