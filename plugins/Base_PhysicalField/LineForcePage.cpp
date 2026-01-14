#include "LineForcePage.h"

#include <QComboBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QTableWidget>

LineForcePage::LineForcePage(QWidget *parent)
    : LoadSpecPageBase(QStringLiteral("线力"),
                       QStringLiteral("线力（NFTX）：节点序列 + Fx/Fy/Fz"),
                       parent)
{
    auto *formLayout = new QFormLayout();
    auto *lxjmEl = new QComboBox(this);
    lxjmEl->addItem(QStringLiteral("未知单元 (LXJMEL=0)"));
    lxjmEl->addItem(QStringLiteral("已知单元 (LXJMEL=1)"));
    formLayout->addRow(QStringLiteral("LXJMEL"), lxjmEl);

    auto *countSpin = new QSpinBox(this);
    countSpin->setMinimum(0);
    countSpin->setMaximum(100000);
    formLayout->addRow(QStringLiteral("NFL"), countSpin);
    layout()->insertLayout(1, formLayout);

    auto *table = createTable({QStringLiteral("线力序号"),
                               QStringLiteral("节点号"),
                               QStringLiteral("Fx"),
                               QStringLiteral("Fy"),
                               QStringLiteral("Fz"),
                               QStringLiteral("相关单元")});
    layout()->insertWidget(2, table);

    connect(countSpin, QOverload<int>::of(&QSpinBox::valueChanged), table, &QTableWidget::setRowCount);
}
