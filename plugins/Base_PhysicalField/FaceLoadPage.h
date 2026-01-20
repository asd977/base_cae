#pragma once

#include "BasePage.h"

class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class QPushButton;

class FaceLoadPage : public BasePage
{
    Q_OBJECT
public:
    explicit FaceLoadPage(QWidget* parent = nullptr);
    ~FaceLoadPage() override;

    void beforeLoad() override;
    void beforeUnload() override;

private slots:
    void onGenerate();

private:
    void dumpGeneratedToDebug();

private:
    // LXJMET: 0/1
    QComboBox*      m_mode = nullptr;

    // 相关单元 KE（仅 LXJMET=1 时有效，先用手输）
    QSpinBox*       m_ke = nullptr;

    // 每个节点的面力分量（N/m^2）
    QDoubleSpinBox* m_fx = nullptr;
    QDoubleSpinBox* m_fy = nullptr;
    QDoubleSpinBox* m_fz = nullptr;

    // II
    QSpinBox*       m_ii = nullptr;

    QPushButton*    m_btnGenerate = nullptr;
};
