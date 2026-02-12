#include "MeshGroupingWidget.h"
#include "ui_MeshGroupingWidget.h"

#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>

#include <QMessageBox>

MeshGroupingWidget::MeshGroupingWidget(QWidget *parent)
    : BasePage("MeshGroupingWidget")
    , ui(new Ui::MeshGroupingWidget)
{
    ui->setupUi(this);

    connect(ui->btnAutoGrouping, &QPushButton::clicked, this, &MeshGroupingWidget::onAutoGrouping);
    connect(ui->btnRangeGrouping, &QPushButton::clicked, this, &MeshGroupingWidget::onRangeGrouping);
}

MeshGroupingWidget::~MeshGroupingWidget()
{
    delete ui;
}

void MeshGroupingWidget::beforeLoad()
{
}

void MeshGroupingWidget::beforeUnload()
{
}

void MeshGroupingWidget::onAutoGrouping()
{
    auto *meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshService) {
        QMessageBox::warning(this, "网格分组", "MeshModuleControllerService 不可用");
        return;
    }

    QString err;
    if (!meshService->autoGroupingBySpatialBins(ui->spAutoGroupCount->value(), &err)) {
        QMessageBox::warning(this, "自动分组失败", err.isEmpty() ? "自动分组失败" : err);
        return;
    }

    QMessageBox::information(this, "自动分组", "自动分组完成，已更新 MATYPE/GroupName。");
}

void MeshGroupingWidget::onRangeGrouping()
{
    auto *meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshService) {
        QMessageBox::warning(this, "网格分组", "MeshModuleControllerService 不可用");
        return;
    }

    const QString groupName = ui->edtGroupName->text().trimmed();
    if (groupName.isEmpty()) {
        QMessageBox::warning(this, "参数错误", "请输入分组名称。");
        return;
    }

    QString err;
    if (!meshService->groupByCoordinateRange(
            ui->dsXMin->value(), ui->dsXMax->value(),
            ui->dsYMin->value(), ui->dsYMax->value(),
            ui->dsZMin->value(), ui->dsZMax->value(),
            ui->spMatype->value(), groupName, &err)) {
        QMessageBox::warning(this, "范围分组失败", err.isEmpty() ? "范围分组失败" : err);
        return;
    }

    QMessageBox::information(this, "范围分组", "范围分组完成，命中单元已更新。");
}
