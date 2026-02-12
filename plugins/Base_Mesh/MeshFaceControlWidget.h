#ifndef MESHFACECONTROLWIDGET_H
#define MESHFACECONTROLWIDGET_H

#include <BasePage.h>

namespace Ui {
class MeshFaceControlWidget;
}

class MeshFaceControlWidget : public BasePage
{
    Q_OBJECT

public:
    explicit MeshFaceControlWidget(QWidget *parent = nullptr);
    ~MeshFaceControlWidget() override;
    void beforeLoad(){};
    void beforeUnload() {};

private:
    Ui::MeshFaceControlWidget *ui;
};

#endif // MESHFACECONTROLWIDGET_H
