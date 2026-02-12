#ifndef MESHEDGECONTROLWIDGET_H
#define MESHEDGECONTROLWIDGET_H

#include <BasePage.h>

namespace Ui {
class MeshEdgeControlWidget;
}

class MeshEdgeControlWidget : public BasePage
{
    Q_OBJECT

public:
    explicit MeshEdgeControlWidget(QWidget *parent = nullptr);
    ~MeshEdgeControlWidget() override;

    void beforeLoad(){};
    void beforeUnload() {};

private:
    Ui::MeshEdgeControlWidget *ui;
};

#endif // MESHEDGECONTROLWIDGET_H
