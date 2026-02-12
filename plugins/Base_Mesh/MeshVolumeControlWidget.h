#ifndef MESHVOLUMECONTROLWIDGET_H
#define MESHVOLUMECONTROLWIDGET_H

#include <BasePage.h>

namespace Ui {
class MeshVolumeControlWidget;
}

class MeshVolumeControlWidget : public BasePage
{
    Q_OBJECT

public:
    explicit MeshVolumeControlWidget(QWidget *parent = nullptr);
    ~MeshVolumeControlWidget() override;

    void beforeLoad();
    void beforeUnload();

private:
    Ui::MeshVolumeControlWidget *ui;
};

#endif // MESHVOLUMECONTROLWIDGET_H
