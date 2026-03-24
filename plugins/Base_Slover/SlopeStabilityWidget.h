#ifndef SLOPESTABILITYWIDGET_H
#define SLOPESTABILITYWIDGET_H

#include <BasePage.h>

#include <QJsonObject>

class QPushButton;
class QLabel;

class SlopeStabilityWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit SlopeStabilityWidget(QWidget* parent = nullptr);

    void beforeLoad() override;
    void beforeUnload() override;

    bool exportIslopDatFile() const;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  signals:
    void computeRequested();

  private:
    QLabel* m_summaryLabel = nullptr;
    QPushButton* m_computeButton = nullptr;
};

#endif // SLOPESTABILITYWIDGET_H
