#pragma once

#include "BasePage.h"

class QDoubleSpinBox;
class QSpinBox;
class QPushButton;

class PointForcePage : public BasePage
{
    Q_OBJECT
public:
    explicit PointForcePage(QWidget* parent = nullptr);
    ~PointForcePage() override;

    void beforeLoad() override;
    void beforeUnload() override;

private slots:
    void onGenerate();

private:
    void dumpGeneratedToDebug();

private:
    QDoubleSpinBox* m_x   = nullptr;
    QDoubleSpinBox* m_y   = nullptr;
    QDoubleSpinBox* m_z   = nullptr;
    QDoubleSpinBox* m_fcx = nullptr;
    QDoubleSpinBox* m_fcy = nullptr;
    QDoubleSpinBox* m_fcz = nullptr;
    QSpinBox*       m_ii  = nullptr;

    QPushButton*    m_btnGenerate = nullptr;
};
