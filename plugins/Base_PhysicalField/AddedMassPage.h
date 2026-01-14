#pragma once

#include "LoadSpecPageBase.h"

class QSpinBox;
class QTableWidget;

class AddedMassPage final : public LoadSpecPageBase
{
public:
    explicit AddedMassPage(QWidget *parent = nullptr);
    void setSelectedFaceIds(const QList<vtkIdType> &faceIds) override;

private:
    QTableWidget *m_table = nullptr;
    QSpinBox *m_countSpin = nullptr;
};
