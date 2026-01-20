#pragma once

#include "BasePage.h"

class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QPushButton;

class BodyForcePage : public BasePage
{
    Q_OBJECT
public:
    explicit BodyForcePage(QWidget* parent = nullptr);
    ~BodyForcePage() override;

    void beforeLoad() override;
    void beforeUnload() override;

private slots:
    void onGenerate();
    void onUniformToggled(bool on);

private:
    void dumpGeneratedToDebug();

private:
    QDoubleSpinBox* m_fbx = nullptr;
    QDoubleSpinBox* m_fby = nullptr;
    QDoubleSpinBox* m_fbz = nullptr;
    QSpinBox*       m_ii  = nullptr;

    QCheckBox*      m_chkUniform = nullptr;
    QPushButton*    m_btnGenerate = nullptr;
};
