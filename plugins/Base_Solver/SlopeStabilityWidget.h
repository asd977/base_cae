#ifndef SLOPESTABILITYWIDGET_H
#define SLOPESTABILITYWIDGET_H

#include <BasePage.h>
#include <QPointer>

#include <QJsonObject>
#include <QJsonArray>
#include <QMap>

class QWidget;
class QCheckBox;
class QuietBoundarySelector;
class QComboBox;
class QLabel;
class QPushButton;
class SlopeExcavationSettings;

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
    void openStandardMethodDialog();
    bool excavationEnabled() const;
    SlopeExcavationSettings* excavationSettings() const { return m_excavation; }
    bool prepareExcavationFiles(const QString& inputDirectory, const QString& stressDirectory, QString* error);

  signals:
    void computeRequested();
    void stopRequested();
    void limitEquilibriumRequested();
    void initialStressRequested();
    void excavationStructureChanged();
    void navigateToExcavationStageRequested(int index);
    void navigateToReinforcementRequested();


  private slots:
    void onComputeClicked();
    void onStandardComputeClicked();
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
    SlopeExcavationSettings* m_excavation=nullptr;
    QComboBox* m_dynamicExcavationShape = nullptr;
    QCheckBox* m_postExcavationReserveCheck = nullptr;
    QPointer<QCheckBox> m_quietBoundaryCheck = nullptr;
    QPointer<QuietBoundarySelector> m_quietBoundarySelection = nullptr;
    QPointer<QLabel> m_quietBoundaryStatus = nullptr;
    QMap<int, QWidget*> m_lineEditors;
    bool exportTdtraceDatFile() const;
    bool exportMaterialMappingsFile() const;
};

#endif // SLOPESTABILITYWIDGET_H
