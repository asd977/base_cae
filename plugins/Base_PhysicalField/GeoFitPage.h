#pragma once

#include "LoadSpecPageBase.h"

class QSpinBox;
class QTableWidget;

class GeoFitPage final : public LoadSpecPageBase
{
public:
    explicit GeoFitPage(QWidget *parent = nullptr);
    void setSelectedFaceIds(const QList<vtkIdType> &faceIds) override;

private:
    QTableWidget *m_table = nullptr;
    QSpinBox *m_countSpin = nullptr;
};
