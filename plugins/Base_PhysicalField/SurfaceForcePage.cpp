#include "SurfaceForcePage.h"

#include <QComboBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QTableWidget>

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

    auto *countSpin = new QSpinBox(this);
    countSpin->setMinimum(0);
    countSpin->setMaximum(100000);
    formLayout->addRow(QStringLiteral("NFT"), countSpin);
    layout()->insertLayout(1, formLayout);

    auto *table = createTable({QStringLiteral("面力序号"),
                               QStringLiteral("节点号"),
                               QStringLiteral("Fx"),
                               QStringLiteral("Fy"),
                               QStringLiteral("Fz"),
                               QStringLiteral("相关单元")});
    layout()->insertWidget(2, table);

    connect(countSpin, QOverload<int>::of(&QSpinBox::valueChanged), table, &QTableWidget::setRowCount);
}
