#include "OutputWidget.h"

#include <QTextCursor>
#include <QMenu>
#include <QAction>
#include <QPushButton>
#include <QScrollBar>
#include <QEvent>

#include "ui_OutputWidget.h"

OutputWidget::OutputWidget(QWidget* parent) : QWidget(parent), ui(new Ui::OutputWidget)
{
    ui->setupUi(this);
    ui->textEdit->setReadOnly(true);
    ui->textEdit->setUndoRedoEnabled(false);

    // Create floating, semi-transparent clear button inside the text edit
    QPushButton* floatingClearBtn = new QPushButton("清空", ui->textEdit);
    floatingClearBtn->setObjectName("floatingClearBtn");
    floatingClearBtn->setCursor(Qt::PointingHandCursor);
    floatingClearBtn->setToolTip("清空日志");
    floatingClearBtn->setFixedSize(44, 22);

    // Visual styling is centralized in app.qss.


    connect(floatingClearBtn, &QPushButton::clicked, this, &OutputWidget::onClearClicked);

    // Listen to resize events on the textEdit to reposition the floating button
    ui->textEdit->installEventFilter(this);

    // Add right-click context menu to clear log
    ui->textEdit->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->textEdit, &QWidget::customContextMenuRequested, this,
            [this](const QPoint& pos)
            {
                QMenu* menu = ui->textEdit->createStandardContextMenu();
                menu->addSeparator();
                QAction* clearAct = menu->addAction("清空日志");
                connect(clearAct, &QAction::triggered, this, &OutputWidget::onClearClicked);
                menu->exec(ui->textEdit->mapToGlobal(pos));
                delete menu;
            });
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

void OutputWidget::onClearClicked()
{
    ui->textEdit->clear();
}

bool OutputWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == ui->textEdit && event->type() == QEvent::Resize)
    {
        if (auto* btn = ui->textEdit->findChild<QPushButton*>("floatingClearBtn"))
        {
            int scrollbarWidth = ui->textEdit->verticalScrollBar()->isVisible() ? ui->textEdit->verticalScrollBar()->width() : 0;
            btn->move(ui->textEdit->width() - btn->width() - scrollbarWidth - 8, 8);
        }
    }
    return QWidget::eventFilter(watched, event);
}

OutputWidget::~OutputWidget()
{
    delete ui;
}
