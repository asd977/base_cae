#ifndef LOADSPECIFICATIONWIDGET_H
#define LOADSPECIFICATIONWIDGET_H

#include <BasePage.h>
#include <QWidget>

namespace Ui {
class LoadSpecificationWidget;
}

class LoadSpecificationWidget : public BasePage
{
    Q_OBJECT

public:
    explicit LoadSpecificationWidget(QWidget *parent = nullptr);
    ~LoadSpecificationWidget();
    void beforeLoad() override;
    void beforeUnload() override;

private:
    Ui::LoadSpecificationWidget *ui;
};

#endif // LOADSPECIFICATIONWIDGET_H
