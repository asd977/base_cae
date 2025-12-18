#include "OutputWidget.h"
#include "ui_OutputWidget.h"

#include <QTextBlock>
#include <services/UiFrameService.h>
#include <ServiceRegistry.h>
#include <QTextCursor.h>

OutputWidget::OutputWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OutputWidget)
{
    ui->setupUi(this);
    ui->textEdit->setReadOnly(true);
    MAX_LINES = 100;
}

void OutputWidget::outputMessage(QString &str)
{
    ui->textEdit->append(str);
    // Limit the number of lines
    QTextDocument *doc = ui->textEdit->document();
    while (doc->blockCount() > MAX_LINES) {
        // Remove the first block (line)
        QTextBlock firstBlock = doc->firstBlock();
        QTextCursor cursor(doc);
        cursor.setPosition(firstBlock.position());
        cursor.movePosition(QTextCursor::StartOfBlock);
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();  // Remove the first block
        cursor.deleteChar();           // Delete the line break
    }
}

void OutputWidget::showEvent(QShowEvent *event)
{
    ui->textEdit->moveCursor(QTextCursor::End);
}

OutputWidget::~OutputWidget()
{
    delete ui;
}
