#include "SeismicPage.h"

#include <QFormLayout>
#include <QHeaderView>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
QWidget *createDirectionTab(const QString &label, QWidget *parent)
{
    auto *container = new QWidget(parent);
    auto *layout = new QVBoxLayout(container);
    auto *formLayout = new QFormLayout();
    auto *countSpin = new QSpinBox(container);
    countSpin->setMinimum(0);
    countSpin->setMaximum(100000);
    formLayout->addRow(label, countSpin);
    layout->addLayout(formLayout);

    auto *table = new QTableWidget(container);
    table->setColumnCount(1);
    table->setHorizontalHeaderLabels({QStringLiteral("加速度")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);
    layout->addWidget(table);

    QObject::connect(countSpin, QOverload<int>::of(&QSpinBox::valueChanged), table, &QTableWidget::setRowCount);
    return container;
}
}

SeismicPage::SeismicPage(QWidget *parent)
    : LoadSpecPageBase(QStringLiteral("地震"),
                       QStringLiteral("地震加速度（NTBX/NTBY/NTBZ）：三向时间序列"),
                       parent)
{
    auto *tabs = new QTabWidget(this);
    tabs->addTab(createDirectionTab(QStringLiteral("NTBX"), tabs), QStringLiteral("X向"));
    tabs->addTab(createDirectionTab(QStringLiteral("NTBY"), tabs), QStringLiteral("Y向"));
    tabs->addTab(createDirectionTab(QStringLiteral("NTBZ"), tabs), QStringLiteral("Z向"));
    layout()->insertWidget(1, tabs);
}
