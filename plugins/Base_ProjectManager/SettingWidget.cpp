#include "Settingwidget.h"

#include "ui_settingwidget.h"

SettingWidget::SettingWidget(QWidget* parent) : QWidget(parent), ui(new Ui::SettingWidget)
{
    ui->setupUi(this);

    setWindowTitle(QStringLiteral("参数"));
    setObjectName(QStringLiteral("settingWorkspace"));

    m_layout = new QHBoxLayout(this);
    m_stackedWidget = new QStackedWidget(this);
    m_stackedWidget->setObjectName(QStringLiteral("projectSettingStackWidget"));
    m_layout->addWidget(m_stackedWidget);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    // The active page name is shown by the ADS dock title. This keeps the page
    // itself free of repeated COMSOL-like title bands while preserving context.
    connect(m_stackedWidget, &QWidget::windowTitleChanged, this,
            [this](const QString& title)
            {
                const QString cleanTitle = title.trimmed();
                if (!cleanTitle.isEmpty())
                    setWindowTitle(cleanTitle);
            });
}

SettingWidget::~SettingWidget()
{
    delete ui;
}
