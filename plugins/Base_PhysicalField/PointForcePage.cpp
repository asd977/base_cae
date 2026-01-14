#include "PointForcePage.h"

#include <QFormLayout>
#include <QSpinBox>
#include <QTableWidget>

PointForcePage::PointForcePage(QWidget *parent)
    : LoadSpecPageBase(QStringLiteral("集中力"),
                       QStringLiteral("集中力（NFC）：单元号 + 坐标 + Fcx/Fcy/Fcz"),
                       parent)
{
    auto *formLayout = new QFormLayout();
    auto *countSpin = new QSpinBox(this);
    countSpin->setMinimum(0);
    countSpin->setMaximum(100000);
    formLayout->addRow(QStringLiteral("NFC"), countSpin);
    layout()->insertLayout(1, formLayout);

    auto *table = createTable({QStringLiteral("KE"),
                               QStringLiteral("X"),
                               QStringLiteral("Y"),
                               QStringLiteral("Z"),
                               QStringLiteral("Fcx"),
                               QStringLiteral("Fcy"),
                               QStringLiteral("Fcz")});
    layout()->insertWidget(2, table);

    connect(countSpin, QOverload<int>::of(&QSpinBox::valueChanged), table, &QTableWidget::setRowCount);
}
