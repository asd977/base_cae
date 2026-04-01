#ifndef CAVEEXCAVATIONWIDGET_H
#define CAVEEXCAVATIONWIDGET_H

#include <BasePage.h>

class QWidget;

namespace Ui
{
class CaveExcavationWidget;
}

class CaveExcavationWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit CaveExcavationWidget(QWidget* parent = nullptr);
    ~CaveExcavationWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

    bool exportICaveDatFile() const;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  signals:
    void computeRequested();

  private slots:
    void onComputeClicked();

  private:
    Ui::CaveExcavationWidget* ui;
};

#endif // CAVEEXCAVATIONWIDGET_H
