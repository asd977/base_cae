#ifndef SOLVERSETTINGWIDGET_H
#define SOLVERSETTINGWIDGET_H

#include <BasePage.h>
#include <QPair>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QComboBox;
class QTreeWidgetItem;

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
    void setBatchAnalyses(const QVector<QPair<QString, QString>>& analyses);
    void setBatchRunning(bool running, int total = 0);
    void setBatchItemStatus(const QString& uuid, const QString& status);
    void setBatchProgress(int finished, int total, int failed = 0);
    void setBatchStatusText(const QString& text);
    QString computeDevice() const;
    void setComputeDevice(const QString& device);
    int gpuDeviceMode() const;

signals:
    void createSeepageRequested();
    void createDamRequested();
    void createSlopeRequested();
    void createCaveRequested();
    void createBlockRequested();
    void batchComputeRequested(const QStringList& analysisUuids);
    void batchStopRequested();
    void computeDeviceChanged(const QString& device);
    void rootActivated();

private:
    void updateDeviceControls();
    QStringList checkedBatchAnalysisUuids() const;
    QTreeWidgetItem* findBatchItem(const QString& uuid) const;

    Ui::SolverSettingWidget* ui;
    QComboBox* m_computeDeviceCombo = nullptr;
    QComboBox* m_gpuModeCombo = nullptr;
    bool m_batchRunning = false;
};

#endif // SOLVERSETTINGWIDGET_H
