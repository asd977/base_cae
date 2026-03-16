#include "DisplayInfoPage.h"

#include "ui_DisplayInfoPage.h"

DisplayInfoPage::DisplayInfoPage(const QString& title, const QStringList& details, QWidget* parent)
    : BasePage("DisplayInfoPage"), ui(new Ui::DisplayInfoPage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    ui->titleLabel->setText(title);
    ui->detailListWidget->clear();
    for (const QString& line : details)
    {
        if (line.trimmed().isEmpty())
            continue;
        ui->detailListWidget->addItem(line);
    }
}

DisplayInfoPage::~DisplayInfoPage()
{
    delete ui;
}
