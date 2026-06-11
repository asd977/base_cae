#ifndef SLOPESTABILITYWIDGET_H
#define SLOPESTABILITYWIDGET_H

#include <BasePage.h>

#include <QJsonObject>
#include <QMap>

class QWidget;

namespace Ui
{
class SlopeStabilityWidget;
}

class SlopeStabilityWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit SlopeStabilityWidget(QWidget* parent = nullptr);
    ~SlopeStabilityWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

    bool exportIslopDatFile() const;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  signals:
    void computeRequested();
    void stopRequested();


  private slots:
    void onComputeClicked();
    void setComputing(bool computing);


  private:
    QString quoteValue(const QString& value) const;
    QString lineValue(int lineNo) const;
    void setLineValue(int lineNo, const QString& value);
    QString defaultValueByLine(int lineNo) const;
    void initStrengthReserveTable();

  private:
    Ui::SlopeStabilityWidget* ui;
    QMap<int, QWidget*> m_lineEditors;
    bool exportTdtraceDatFile() const;
};

#endif // SLOPESTABILITYWIDGET_H
