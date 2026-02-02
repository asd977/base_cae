#pragma once

#include "BasePage.h"

#include <QVector>

class QDoubleSpinBox;
class QSpinBox;

class BodyForcePage : public BasePage
{
    Q_OBJECT
public:
    explicit BodyForcePage(QWidget* parent=nullptr);
    ~BodyForcePage() override;

    void beforeLoad() override;
    void beforeUnload() override;

    struct Part3Row
    {
        int jd = 0;          // node_id；均布体力：jd=0
        double fbx = 0.0;
        double fby = 0.0;
        double fbz = 0.0;
    };

    // 生成 Part3 的数据行（按规范：JD, FBX, FBY, FBZ）
    QVector<Part3Row> buildPart3Rows() const;

    // Part3 header 的 II（没有控件则默认 1）
    int ii() const;

private:
    QDoubleSpinBox* m_fbx = nullptr;
    QDoubleSpinBox* m_fby = nullptr;
    QDoubleSpinBox* m_fbz = nullptr;

    // UI 里如果你加了 spinII，就会自动拾取；否则为 nullptr，默认 1
    QSpinBox* m_ii = nullptr;
};
