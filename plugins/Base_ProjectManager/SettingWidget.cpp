#include "Settingwidget.h"

#include "ui_settingwidget.h"
#include <SettingsLayout.h>
#include <QScrollArea>

SettingWidget::SettingWidget(QWidget* parent) : QWidget(parent), ui(new Ui::SettingWidget)
{
    ui->setupUi(this);

    setWindowTitle(QStringLiteral("参数"));
    setObjectName(QStringLiteral("settingWorkspace"));
    setProperty("dockForceNoScrollArea",true);
    setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Expanding);

    m_layout = new QHBoxLayout(this);
    auto* scroll=new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("settingsPageScrollArea"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Expanding);
    m_stackedWidget = new CaeSettingsLayout::ActivePageStack(scroll);
    m_stackedWidget->setObjectName(QStringLiteral("projectSettingStackWidget"));
    scroll->setWidget(m_stackedWidget);
    m_layout->addWidget(scroll);
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
