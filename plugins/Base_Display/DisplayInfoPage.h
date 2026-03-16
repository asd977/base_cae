#ifndef DISPLAYINFOPAGE_H
#define DISPLAYINFOPAGE_H

#include <BasePage.h>

#include <QStringList>
#include <QVector>

namespace Ui
{
class DisplayInfoPage;
}

class DisplayInfoPage : public BasePage
{
    Q_OBJECT

  public:
    explicit DisplayInfoPage(const QString& title, const QString& intro, const QStringList& details,
                             const QStringList& tableHeaders = {},
                             const QVector<QStringList>& tableRows = {}, QWidget* parent = nullptr);
    ~DisplayInfoPage();

    void beforeLoad() override
    {
    }

    void beforeUnload() override
    {
    }

  private:
    void setupTable(const QStringList& tableHeaders, const QVector<QStringList>& tableRows);

  private:
    Ui::DisplayInfoPage* ui;
};

#endif // DISPLAYINFOPAGE_H
