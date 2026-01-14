#include "AddedMassPage.h"

#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QTableWidget>

AddedMassPage::AddedMassPage(QWidget *parent)
    : LoadSpecPageBase(QStringLiteral("附加质量"),
                       QStringLiteral("附加质量：NMM/MAKOD/水位顶底/面定义"),
                       parent)
{
    auto *formLayout = new QFormLayout();
    auto *nmmSpin = new QSpinBox(this);
    nmmSpin->setMinimum(0);
    nmmSpin->setMaximum(100000);
    formLayout->addRow(QStringLiteral("NMM"), nmmSpin);

    auto *makodEdit = new QLineEdit(this);
    formLayout->addRow(QStringLiteral("MAKOD"), makodEdit);

    auto *hwupTopEdit = new QLineEdit(this);
    formLayout->addRow(QStringLiteral("HWUP_TOP"), hwupTopEdit);

    auto *hwupBotEdit = new QLineEdit(this);
    formLayout->addRow(QStringLiteral("HWUP_BOT"), hwupBotEdit);

    auto *ifwestEdit = new QLineEdit(this);
    formLayout->addRow(QStringLiteral("IFWEST"), ifwestEdit);

    layout()->insertLayout(1, formLayout);

    auto *table = createTable({QStringLiteral("面序号"),
                               QStringLiteral("KE1"),
                               QStringLiteral("KE2"),
                               QStringLiteral("JD1"),
                               QStringLiteral("JD2"),
                               QStringLiteral("JD3"),
                               QStringLiteral("JD4")});
    layout()->insertWidget(2, table);

    connect(nmmSpin, QOverload<int>::of(&QSpinBox::valueChanged), table, &QTableWidget::setRowCount);
}
