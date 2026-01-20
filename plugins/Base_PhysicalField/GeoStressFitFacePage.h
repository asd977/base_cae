#pragma once

#include "BasePage.h"

class QLineEdit;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QPushButton;

class GeoStressFitFacePage : public BasePage
{
    Q_OBJECT
public:
    explicit GeoStressFitFacePage(QWidget* parent = nullptr);
    ~GeoStressFitFacePage() override;

    void beforeLoad() override;
    void beforeUnload() override;

private slots:
    void onGenerate();

private:
    void dumpGeneratedToDebug();

private:
    // UI pointers
    QLineEdit*      m_fname = nullptr;
    QComboBox*      m_idir  = nullptr;
    QDoubleSpinBox* m_height = nullptr;
    QDoubleSpinBox* m_rou    = nullptr;
    QDoubleSpinBox* m_pgrout = nullptr;
    QSpinBox*       m_ii     = nullptr;

    QPushButton*    m_btnGenerate = nullptr;
};
