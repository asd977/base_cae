#pragma once

#include "BasePage.h"

class QDoubleSpinBox;
class QSpinBox;
class QPushButton;

class TemperatureLoadPage : public BasePage
{
    Q_OBJECT
public:
    explicit TemperatureLoadPage(QWidget* parent = nullptr);
    ~TemperatureLoadPage() override;

    void beforeLoad() override;
    void beforeUnload() override;

private slots:
    void onGenerate();

private:
    void dumpGeneratedToDebug();

private:
    QDoubleSpinBox* m_alfa = nullptr;   // 热值导系数/换热系数(按你文档写法：ALFA)
    QDoubleSpinBox* m_dt   = nullptr;   // 变温
    QSpinBox*       m_ii   = nullptr;   // II

    QPushButton*    m_btnGenerate = nullptr;
};
