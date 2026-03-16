#ifndef DISPLAYINFOPAGE_H
#define DISPLAYINFOPAGE_H

#include <BasePage.h>

namespace Ui
{
class DisplayInfoPage;
}

class DisplayInfoPage : public BasePage
{
    Q_OBJECT

  public:
    explicit DisplayInfoPage(const QString& title, const QStringList& details, QWidget* parent = nullptr);
    ~DisplayInfoPage();

    void beforeLoad() override
    {
    }

    void beforeUnload() override
    {
    }

  private:
    Ui::DisplayInfoPage* ui;
};

#endif // DISPLAYINFOPAGE_H
