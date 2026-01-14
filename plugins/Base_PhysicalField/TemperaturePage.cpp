#include "TemperaturePage.h"

#include <QFormLayout>
#include <QSpinBox>
#include <QTableWidget>

TemperaturePage::TemperaturePage(QWidget *parent)
    : LoadSpecPageBase(QStringLiteral("变温"),
                       QStringLiteral("变温（NFW）：节点号 + ALFA + DT"),
                       parent)
{
    auto *formLayout = new QFormLayout();
    auto *countSpin = new QSpinBox(this);
    countSpin->setMinimum(0);
    countSpin->setMaximum(100000);
    formLayout->addRow(QStringLiteral("NFW"), countSpin);
    layout()->insertLayout(1, formLayout);

    auto *table = createTable({QStringLiteral("节点号"),
                               QStringLiteral("ALFA"),
                               QStringLiteral("DT")});
    layout()->insertWidget(2, table);

    connect(countSpin, QOverload<int>::of(&QSpinBox::valueChanged), table, &QTableWidget::setRowCount);
}
