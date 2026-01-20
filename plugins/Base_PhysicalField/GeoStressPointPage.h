#ifndef GEOSTRESSPOINTPAGE_H
#define GEOSTRESSPOINTPAGE_H

#include "BasePage.h"
#include "PickManager.h"

class QDoubleSpinBox;
class QPushButton;

class GeoStressPointPage : public BasePage
{
    Q_OBJECT
public:
    explicit GeoStressPointPage(QWidget* parent = nullptr);
    ~GeoStressPointPage() override;

    void beforeLoad() override;
    void beforeUnload() override;

private slots:
    void onGenerate(); // 生成并 qDebug 输出

private:
    void dumpGeneratedToDebug();

private:
    // UI: 统一应力 6 分量
    QDoubleSpinBox* m_sx  = nullptr;
    QDoubleSpinBox* m_sy  = nullptr;
    QDoubleSpinBox* m_sz  = nullptr;
    QDoubleSpinBox* m_sxy = nullptr;
    QDoubleSpinBox* m_sxz = nullptr;
    QDoubleSpinBox* m_syz = nullptr;

    QPushButton* m_btnGenerate = nullptr;
};

#endif // GEOSTRESSPOINTPAGE_H
