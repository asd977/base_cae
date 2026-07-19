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
    bool validateInputs(QString* errorMessage) const;

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
    QString gravityLineValue() const;
    void setGravityLineValue(const QString& value);
    QString defaultValueByLine(int lineNo) const;
    void setupStableModeSections();
    void initStrengthReserveTable();
    void setStrengthReserveOutputCombo(int row, const QString& value);
    QString strengthReserveOutputValue(int row) const;
    void updateAnalysisModeVisibility();
    void updateTimeHistoryVisibility();
    void updateMethodVisibility();
    void updateDampingControls();

  private:
    Ui::SlopeStabilityWidget* ui;
    QMap<int, QWidget*> m_lineEditors;
    bool exportTdtraceDatFile() const;
    bool exportMaterialMappingsFile() const;
};

#endif // SLOPESTABILITYWIDGET_H
