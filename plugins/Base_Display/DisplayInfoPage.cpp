#include "DisplayInfoPage.h"

#include <QHeaderView>

#include "ui_DisplayInfoPage.h"

DisplayInfoPage::DisplayInfoPage(const QString& title, const QString& intro,
                                 const QStringList& details, const QStringList& tableHeaders,
                                 const QVector<QStringList>& tableRows, QWidget* parent)
    : BasePage("DisplayInfoPage"), ui(new Ui::DisplayInfoPage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    ui->titleLabel->setText(title);
    ui->introLabel->setText(intro);

    ui->detailListWidget->clear();
    for (const QString& line : details)
    {
        if (!line.trimmed().isEmpty())
            ui->detailListWidget->addItem(line);
    }

    setupTable(tableHeaders, tableRows);
}

DisplayInfoPage::~DisplayInfoPage()
{
    delete ui;
}

void DisplayInfoPage::setupTable(const QStringList& tableHeaders,
                                 const QVector<QStringList>& tableRows)
{
    if (tableHeaders.isEmpty() || tableRows.isEmpty())
    {
        ui->tableTitleLabel->setVisible(false);
        ui->tableWidget->setVisible(false);
        return;
    }

    ui->tableTitleLabel->setVisible(true);
    ui->tableWidget->setVisible(true);
    ui->tableWidget->setColumnCount(tableHeaders.size());
    ui->tableWidget->setHorizontalHeaderLabels(tableHeaders);
    ui->tableWidget->setRowCount(tableRows.size());
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidget->verticalHeader()->setVisible(false);
    ui->tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableWidget->setSelectionMode(QAbstractItemView::NoSelection);

    for (int row = 0; row < tableRows.size(); ++row)
    {
        const QStringList& cols = tableRows.at(row);
        for (int col = 0; col < tableHeaders.size(); ++col)
        {
            const QString text = col < cols.size() ? cols.at(col) : QString();
            ui->tableWidget->setItem(row, col, new QTableWidgetItem(text));
        }
    }
}
