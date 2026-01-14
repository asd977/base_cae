#include "GeoMeasurePage.h"

#include <QFormLayout>
#include <QSpinBox>
#include <QTableWidget>

GeoMeasurePage::GeoMeasurePage(QWidget *parent)
    : LoadSpecPageBase(QStringLiteral("地应力测点"),
                       QStringLiteral("现场地应力测点（NUM_GEO）：坐标 + Sx/Sy/Txy"),
                       parent)
{
    auto *formLayout = new QFormLayout();
    auto *countSpin = new QSpinBox(this);
    countSpin->setMinimum(0);
    countSpin->setMaximum(100000);
    formLayout->addRow(QStringLiteral("NUM_GEO"), countSpin);
    layout()->insertLayout(1, formLayout);

    auto *table = createTable({QStringLiteral("X"),
                               QStringLiteral("Y"),
                               QStringLiteral("Sx"),
                               QStringLiteral("Sy"),
                               QStringLiteral("Txy")});
    layout()->insertWidget(2, table);

    connect(countSpin, QOverload<int>::of(&QSpinBox::valueChanged), table, &QTableWidget::setRowCount);
}
