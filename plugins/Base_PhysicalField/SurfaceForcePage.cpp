#include "SurfaceForcePage.h"

#include <QComboBox>
#include <QFormLayout>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>

SurfaceForcePage::SurfaceForcePage(QWidget *parent)
    : LoadSpecPageBase(QStringLiteral("面力"),
                       QStringLiteral("面力（NFT）：面节点 + Fx/Fy/Fz"),
                       parent)
{
    auto *formLayout = new QFormLayout();
    auto *lxjmEt = new QComboBox(this);
    lxjmEt->addItem(QStringLiteral("未知单元 (LXJMET=0)"));
    lxjmEt->addItem(QStringLiteral("已知单元 (LXJMET=1)"));
    formLayout->addRow(QStringLiteral("LXJMET"), lxjmEt);

    m_countSpin = new QSpinBox(this);
    m_countSpin->setMinimum(0);
    m_countSpin->setMaximum(100000);
    formLayout->addRow(QStringLiteral("NFT"), m_countSpin);
    layout()->insertLayout(1, formLayout);

    m_table = createTable({QStringLiteral("面力序号"),
                           QStringLiteral("节点号"),
                           QStringLiteral("Fx"),
                           QStringLiteral("Fy"),
                           QStringLiteral("Fz"),
                           QStringLiteral("相关单元")});
    layout()->insertWidget(2, m_table);

    connect(m_countSpin, QOverload<int>::of(&QSpinBox::valueChanged), m_table, &QTableWidget::setRowCount);
}

void SurfaceForcePage::setSelectedFaceIds(const QList<vtkIdType> &faceIds)
{
    if (!m_table || !m_countSpin)
        return;

    QSignalBlocker blocker(m_countSpin);
    m_countSpin->setValue(faceIds.size());
    m_table->setRowCount(faceIds.size());

    for (int row = 0; row < faceIds.size(); ++row) {
        auto *item = new QTableWidgetItem(QString::number(faceIds.at(row)));
        m_table->setItem(row, 0, item);
    }
}
