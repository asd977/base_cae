#include "AddedMassPage.h"

#include <QFormLayout>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>

AddedMassPage::AddedMassPage(QWidget *parent)
    : LoadSpecPageBase(QStringLiteral("附加质量"),
                       QStringLiteral("附加质量：NMM/MAKOD/水位顶底/面定义"),
                       parent)
{
    auto *formLayout = new QFormLayout();
    m_countSpin = new QSpinBox(this);
    m_countSpin->setMinimum(0);
    m_countSpin->setMaximum(100000);
    formLayout->addRow(QStringLiteral("NMM"), m_countSpin);

    auto *makodEdit = new QLineEdit(this);
    formLayout->addRow(QStringLiteral("MAKOD"), makodEdit);

    auto *hwupTopEdit = new QLineEdit(this);
    formLayout->addRow(QStringLiteral("HWUP_TOP"), hwupTopEdit);

    auto *hwupBotEdit = new QLineEdit(this);
    formLayout->addRow(QStringLiteral("HWUP_BOT"), hwupBotEdit);

    auto *ifwestEdit = new QLineEdit(this);
    formLayout->addRow(QStringLiteral("IFWEST"), ifwestEdit);

    layout()->insertLayout(1, formLayout);

    m_table = createTable({QStringLiteral("面序号"),
                           QStringLiteral("KE1"),
                           QStringLiteral("KE2"),
                           QStringLiteral("JD1"),
                           QStringLiteral("JD2"),
                           QStringLiteral("JD3"),
                           QStringLiteral("JD4")});
    layout()->insertWidget(2, m_table);

    connect(m_countSpin, QOverload<int>::of(&QSpinBox::valueChanged), m_table, &QTableWidget::setRowCount);
}

void AddedMassPage::setSelectedFaceIds(const QList<vtkIdType> &faceIds)
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
