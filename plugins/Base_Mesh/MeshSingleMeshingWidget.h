#ifndef MESHSINGLEMESHINGWIDGET_H
#define MESHSINGLEMESHINGWIDGET_H

#include <BasePage.h>

namespace Ui {
class MeshSingleMeshingWidget;
}

class MeshSingleMeshingWidget : public BasePage
{
    Q_OBJECT

public:
    explicit MeshSingleMeshingWidget(QWidget *parent = nullptr);
    ~MeshSingleMeshingWidget() override;

private:
    Ui::MeshSingleMeshingWidget *ui;
};

#endif // MESHSINGLEMESHINGWIDGET_H
