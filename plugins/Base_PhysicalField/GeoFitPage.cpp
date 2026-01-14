#include "GeoFitPage.h"

#include <QFormLayout>
#include <QSpinBox>
#include <QTableWidget>

GeoFitPage::GeoFitPage(QWidget *parent)
    : LoadSpecPageBase(QStringLiteral("地应力拟合"),
                       QStringLiteral("拟合边界面（NFA_GEO）：面文件名 + IDIR + HEIGHT + GAMA + PGROUT"),
                       parent)
{
    auto *formLayout = new QFormLayout();
    auto *countSpin = new QSpinBox(this);
    countSpin->setMinimum(0);
    countSpin->setMaximum(100000);
    formLayout->addRow(QStringLiteral("NFA_GEO"), countSpin);
    layout()->insertLayout(1, formLayout);

    auto *table = createTable({QStringLiteral("FNAME"),
                               QStringLiteral("IDIR"),
                               QStringLiteral("HEIGHT"),
                               QStringLiteral("GAMA"),
                               QStringLiteral("PGROUT"),
                               QStringLiteral("II")});
    layout()->insertWidget(2, table);

    connect(countSpin, QOverload<int>::of(&QSpinBox::valueChanged), table, &QTableWidget::setRowCount);
}
