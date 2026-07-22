#ifndef SOLVERSETTINGWIDGET_H
#define SOLVERSETTINGWIDGET_H

#include <BasePage.h>
#include <QWidget>

class QComboBox;

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

signals:
    void createSeepageRequested();
    void createDamRequested();
    void createSlopeRequested();
    void createCaveRequested();
    void createBlockRequested();
    void computeDeviceChanged(const QString& device);

private:
    Ui::SolverSettingWidget* ui;
    QComboBox* m_computeDeviceCombo = nullptr;
};

#endif // SOLVERSETTINGWIDGET_H
