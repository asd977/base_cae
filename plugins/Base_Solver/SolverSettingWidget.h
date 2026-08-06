#ifndef SOLVERSETTINGWIDGET_H
#define SOLVERSETTINGWIDGET_H

#include <BasePage.h>
#include <QWidget>

class QComboBox;
class QSpinBox;

namespace Ui {
class SolverSettingWidget;
}

class SolverSettingWidget : public BasePage
{
    Q_OBJECT

public:
    explicit SolverSettingWidget(QWidget* parent = nullptr);
    ~SolverSettingWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

    void setAnalysisCounts(int seepageCount, int damCount, int slopeCount,
                           int caveCount, int blockCount);
    QString computeDevice() const;
    void setComputeDevice(const QString& device);
    int gpuDeviceMode() const;
    int assemblyLogInterval() const;
    int solverLogInterval() const;

signals:
    void createSeepageRequested();
    void createDamRequested();
    void createSlopeRequested();
    void createCaveRequested();
    void createBlockRequested();
    void computeDeviceChanged(const QString& device);

private:
    void updateDeviceControls();

    Ui::SolverSettingWidget* ui;
    QComboBox* m_computeDeviceCombo = nullptr;
    QComboBox* m_gpuModeCombo = nullptr;
    QSpinBox* m_assemblyLogSpin = nullptr;
    QSpinBox* m_solverLogSpin = nullptr;
};

#endif // SOLVERSETTINGWIDGET_H
