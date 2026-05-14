#include "OutputWidget.h"

#include <QTextCursor>

#include "ui_OutputWidget.h"

OutputWidget::OutputWidget(QWidget* parent) : QWidget(parent), ui(new Ui::OutputWidget)
{
    ui->setupUi(this);
    ui->textEdit->setReadOnly(true);
    ui->textEdit->setUndoRedoEnabled(false);
    ui->levelFilterComboBox->setCurrentIndex(0);
    connect(ui->levelFilterComboBox, SIGNAL(currentIndexChanged(int)), this,
            SLOT(onFilterChanged(int)));
    connect(ui->clearPushButton, SIGNAL(clicked()), this, SLOT(onClearClicked()));
}

void OutputWidget::outputMessage(const QString& str)
{
    ui->textEdit->append(str.toHtmlEscaped());
    ui->textEdit->moveCursor(QTextCursor::End);
}

void OutputWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    ui->textEdit->moveCursor(QTextCursor::End);
}

void OutputWidget::onFilterChanged(int)
{
}

void OutputWidget::onClearClicked()
{
    ui->textEdit->clear();
}



OutputWidget::~OutputWidget()
{
    delete ui;
}
