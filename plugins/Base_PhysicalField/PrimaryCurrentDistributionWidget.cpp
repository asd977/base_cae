#include "PrimaryCurrentDistributionWidget.h"
#include "ui_PrimaryCurrentDistributionWidget.h"

#include <ElectrolyteSettingWidget.h>
#include <ProjectNode.h>

PrimaryCurrentDistributionWidget::PrimaryCurrentDistributionWidget(QWidget *parent)
    : ui(new Ui::PrimaryCurrentDistributionWidget),
    BasePage("PrimaryCurrentDistributionWidget")
{
    ui->setupUi(this);
}

PrimaryCurrentDistributionWidget::~PrimaryCurrentDistributionWidget()
{
    delete ui;
}

void PrimaryCurrentDistributionWidget::beforeLoad()
{
    //页面加载前
}

void PrimaryCurrentDistributionWidget::beforeUnload()
{
    //页面退出后
}

void PrimaryCurrentDistributionWidget::setNode(ProjectNode *node)
{
    m_node = node;
}

void PrimaryCurrentDistributionWidget::on_electrolytePushButton_clicked()
{
    //添加电解质
    ElectrolyteSettingWidget *electrolyteSettingWidget= new ElectrolyteSettingWidget;

    ProjectNode *node2 = new ProjectNode("电解质",QUuid::createUuid().toString(),(BasePage*)electrolyteSettingWidget);
    m_node->addChild(node2);
}


void PrimaryCurrentDistributionWidget::on_electrodeSurfacePushButton_clicked()
{
    //添加电极表面
}


void PrimaryCurrentDistributionWidget::on_insulationPushButton_clicked()
{
    //添加绝缘面
}

