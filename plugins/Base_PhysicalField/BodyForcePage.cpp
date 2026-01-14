#include "BodyForcePage.h"

#include <QFormLayout>
#include <QSpinBox>
#include <QTableWidget>

BodyForcePage::BodyForcePage(QWidget *parent)
    : LoadSpecPageBase(QStringLiteral("体积力"),
                       QStringLiteral("体积力（NFB）：节点编号 + Fbx/Fby/Fbz"),
                       parent)
{
    auto *formLayout = new QFormLayout();
    auto *countSpin = new QSpinBox(this);
    countSpin->setMinimum(0);
    countSpin->setMaximum(100000);
    formLayout->addRow(QStringLiteral("NFB"), countSpin);
    layout()->insertLayout(1, formLayout);

    auto *table = createTable({QStringLiteral("JD"),
                               QStringLiteral("Fbx"),
                               QStringLiteral("Fby"),
                               QStringLiteral("Fbz")});
    layout()->insertWidget(2, table);

    connect(countSpin, QOverload<int>::of(&QSpinBox::valueChanged), table, &QTableWidget::setRowCount);
}
