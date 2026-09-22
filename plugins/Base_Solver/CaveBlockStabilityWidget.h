#pragma once

#include <BasePage.h>

class CaveBlockPlanePage;
class QLineEdit;
class QDoubleSpinBox;
class QLabel;

namespace Ui
{
class CaveBlockStabilityWidget;
}

class CaveBlockStabilityWidget : public BasePage
{
    Q_OBJECT
  public:
    explicit CaveBlockStabilityWidget(QWidget* parent = nullptr);
    ~CaveBlockStabilityWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

    bool exportDatFiles(QVector<CaveBlockPlanePage*>& planePages);
    bool validateInputs(QString* errorMessage) const;
    void setBlockGroupId(const QString& groupId);
  signals:
    void computeRequested();
    void stopRequested();

  public slots:
    void setComputing(bool computing);

  private:
    QString quoteValue(QString value);
    QString valueOrDefault(QLineEdit* edit, QString def);
    QString valueOrDefault(QDoubleSpinBox* edit, QString def);
    int spatialDimension() const;
    void refreshDimensionUi();
    void updateDirectionEditors();
    void updateRangeSummary();
    void syncPreviewSearchBounds();

  private:
    Ui::CaveBlockStabilityWidget* ui = nullptr;
    QString m_blockGroupId;
    bool m_pageActive = false;
    QLabel* m_rangeSummaryLabel = nullptr;
    QLabel* m_forceInfoLabel = nullptr;
};
