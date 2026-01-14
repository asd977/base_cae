#include "BeamLoadPage.h"

#include <QFormLayout>
#include <QSpinBox>
#include <QTableWidget>

BeamLoadPage::BeamLoadPage(QWidget *parent)
    : LoadSpecPageBase(QStringLiteral("梁单元"),
                       QStringLiteral("梁单元（NFG）：LDTYPE + LOCATION + LOAD + ACTION_PLANE"),
                       parent)
{
    auto *formLayout = new QFormLayout();
    auto *countSpin = new QSpinBox(this);
    countSpin->setMinimum(0);
    countSpin->setMaximum(100000);
    formLayout->addRow(QStringLiteral("NFG"), countSpin);
    layout()->insertLayout(1, formLayout);

    auto *table = createTable({QStringLiteral("KE"),
                               QStringLiteral("X"),
                               QStringLiteral("Y"),
                               QStringLiteral("Z"),
                               QStringLiteral("LDTYPE"),
                               QStringLiteral("LOCATION"),
                               QStringLiteral("LOAD"),
                               QStringLiteral("ACTION_PLANE"),
                               QStringLiteral("IDIR")});
    layout()->insertWidget(2, table);

    connect(countSpin, QOverload<int>::of(&QSpinBox::valueChanged), table, &QTableWidget::setRowCount);
}
