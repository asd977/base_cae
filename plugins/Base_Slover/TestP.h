#ifndef TESTP_H
#define TESTP_H

#include <BasePage.h>
#include <QWidget>

namespace Ui {
class TestPage;
}

class TestPage : public BasePage
{
    Q_OBJECT

public:
    explicit TestPage(QWidget *parent = nullptr);
    ~TestPage();

    void beforeLoad() override;
    void beforeUnload() override;

private:
    Ui::TestPage *ui;
};

#endif // TESTP_H
