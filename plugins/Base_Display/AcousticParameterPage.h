#ifndef ACOUSTICPARAMETERPAGE_H
#define ACOUSTICPARAMETERPAGE_H

#include <BasePage.h>

namespace Ui
{
class AcousticParameterPage;
}

class AcousticParameterPage : public BasePage
{
    Q_OBJECT

  public:
    explicit AcousticParameterPage(QWidget* parent = nullptr);
    ~AcousticParameterPage();

    void beforeLoad() override {}
    void beforeUnload() override {}

  private:
    Ui::AcousticParameterPage* ui;
};

#endif // ACOUSTICPARAMETERPAGE_H
