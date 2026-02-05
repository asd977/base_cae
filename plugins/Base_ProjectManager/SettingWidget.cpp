#include "Settingwidget.h"
#include "ui_settingwidget.h"

SettingWidget::SettingWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingWidget)
{
    ui->setupUi(this);
	m_layout = new QHBoxLayout(this);
	m_stackedWidget = new QStackedWidget(this);
	m_layout->addWidget(m_stackedWidget);
    m_layout->setContentsMargins(0, 0, 0, 0);
}

SettingWidget::~SettingWidget()
{
    delete ui;
}
