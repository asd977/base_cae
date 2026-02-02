#ifndef LINEARFORCEPAGE_H
#define LINEARFORCEPAGE_H

#include <BasePage.h>
#include <QWidget>

namespace Ui {
class LinearForcePage;
}

class LinearForcePage : public BasePage
{
    Q_OBJECT

public:
    explicit LinearForcePage(QWidget *parent = nullptr);
    ~LinearForcePage();

    void beforeLoad();
    void beforeUnload();

    double fxValue();
    double fyValue();
    double fzValue();

private:
    Ui::LinearForcePage *ui;
};

#endif // LINEARFORCEPAGE_H
