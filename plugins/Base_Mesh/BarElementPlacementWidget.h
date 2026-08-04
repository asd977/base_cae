#pragma once

#include <BasePage.h>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QTableWidget;

class BarElementPlacementWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit BarElementPlacementWidget(QWidget* parent = nullptr);

    void beforeLoad() override;
    void beforeUnload() override;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

    void refreshData();

  private slots:
    void createBars();
    void removeSelectedRecord();

  private:
    bool syncPickedEndpoints(bool showWarning);
    void updatePickedEndpointState();
    void refreshRecords();
    QDoubleSpinBox* coordinateSpin(double value = 0.0);

    QDoubleSpinBox* m_start[3]{};
    QDoubleSpinBox* m_end[3]{};
    QDoubleSpinBox* m_offset[3]{};
    QSpinBox* m_segments = nullptr;
    QSpinBox* m_arrayCount = nullptr;
    QSpinBox* m_layer = nullptr;
    QLabel* m_pickStatus = nullptr;
    QLabel* m_actionStatus = nullptr;
    QTableWidget* m_records = nullptr;
};
