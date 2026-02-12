#ifndef MESHGROUPINGWIDGET_H
#define MESHGROUPINGWIDGET_H

#include <BasePage.h>

namespace Ui {
class MeshGroupingWidget;
}

class MeshGroupingWidget : public BasePage
{
    Q_OBJECT

public:
    explicit MeshGroupingWidget(QWidget *parent = nullptr);
    ~MeshGroupingWidget();

    void beforeLoad() override;
    void beforeUnload() override;

private slots:
    void onAutoGrouping();
    void onRangeGrouping();

private:
    Ui::MeshGroupingWidget *ui;
};

#endif // MESHGROUPINGWIDGET_H
