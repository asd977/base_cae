#include "GeoFitPage.h"

#include <QFormLayout>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>

GeoFitPage::GeoFitPage(QWidget *parent)
    : LoadSpecPageBase(QStringLiteral("地应力拟合"),
                       QStringLiteral("拟合边界面（NFA_GEO）：面文件名 + IDIR + HEIGHT + GAMA + PGROUT"),
                       parent)
{
    auto *formLayout = new QFormLayout();
    m_countSpin = new QSpinBox(this);
    m_countSpin->setMinimum(0);
    m_countSpin->setMaximum(100000);
    formLayout->addRow(QStringLiteral("NFA_GEO"), m_countSpin);
    layout()->insertLayout(1, formLayout);

    m_table = createTable({QStringLiteral("FNAME"),
                           QStringLiteral("IDIR"),
                           QStringLiteral("HEIGHT"),
                           QStringLiteral("GAMA"),
                           QStringLiteral("PGROUT"),
                           QStringLiteral("II")});
    layout()->insertWidget(2, m_table);

    connect(m_countSpin, QOverload<int>::of(&QSpinBox::valueChanged), m_table, &QTableWidget::setRowCount);
}

void GeoFitPage::setSelectedFaceIds(const QList<vtkIdType> &faceIds)
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
