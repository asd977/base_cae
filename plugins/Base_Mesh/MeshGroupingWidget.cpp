#include "MeshGroupingWidget.h"
#include "ui_MeshGroupingWidget.h"

#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/VisualWidgetService.h>

#include <QMessageBox>
#include <QMap>
#include <QTableWidgetItem>

#include <vtkActor.h>
#include <vtkCubeSource.h>
#include <vtkIntArray.h>
#include <vtkNew.h>
#include <vtkOutlineFilter.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkStringArray.h>
#include <vtkUnstructuredGrid.h>
#include <vtkCellData.h>

namespace {
const QString kRangePreviewPropId = QStringLiteral("mesh.grouping.range.preview");
}

MeshGroupingWidget::MeshGroupingWidget(QWidget *parent)
    : BasePage("MeshGroupingWidget")
    , ui(new Ui::MeshGroupingWidget)
{
    ui->setupUi(this);

    connect(ui->btnAutoGrouping, &QPushButton::clicked, this, &MeshGroupingWidget::onAutoGrouping);
    connect(ui->btnRangeGrouping, &QPushButton::clicked, this, &MeshGroupingWidget::onRangeGrouping);
    connect(ui->btnRefreshGroups, &QPushButton::clicked, this, &MeshGroupingWidget::onRefreshGroups);
    connect(ui->chkShowRangeBox, &QCheckBox::toggled, this, &MeshGroupingWidget::onToggleRangePreview);

    auto bindRangeChanged = [this](QDoubleSpinBox* s) {
        connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &MeshGroupingWidget::onRangeValueChanged);
    };
    bindRangeChanged(ui->dsXMin);
    bindRangeChanged(ui->dsXMax);
    bindRangeChanged(ui->dsYMin);
    bindRangeChanged(ui->dsYMax);
    bindRangeChanged(ui->dsZMin);
    bindRangeChanged(ui->dsZMax);

    refreshGroupList();
}

MeshGroupingWidget::~MeshGroupingWidget()
{
    clearRangePreview(false);
    delete ui;
}

void MeshGroupingWidget::beforeLoad()
{
    refreshGroupList();
    if (ui->chkShowRangeBox->isChecked()) {
        updateRangePreview(true);
    }
}

void MeshGroupingWidget::beforeUnload()
{
    clearRangePreview();
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

    refreshGroupList();
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

    refreshGroupList();
    QMessageBox::information(this, "范围分组", "范围分组完成，命中单元已更新。");
}

void MeshGroupingWidget::onRefreshGroups()
{
    refreshGroupList();
}

void MeshGroupingWidget::onToggleRangePreview(bool on)
{
    if (on) updateRangePreview(true);
    else clearRangePreview();
}

void MeshGroupingWidget::onRangeValueChanged()
{
    if (ui->chkShowRangeBox->isChecked()) {
        updateRangePreview(true);
    }
}

void MeshGroupingWidget::refreshGroupList()
{
    auto *meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshService) {
        ui->tblGroups->setRowCount(0);
        return;
    }

    vtkSmartPointer<vtkUnstructuredGrid> grid = meshService->getVtkUnstructuredGridSolid();
    if (!grid || grid->GetNumberOfCells() <= 0) {
        grid = meshService->getVtkUnstructuredGrid();
    }

    ui->tblGroups->clearContents();
    ui->tblGroups->setRowCount(0);

    if (!grid || grid->GetNumberOfCells() <= 0) {
        return;
    }

    auto *matypeArr = vtkIntArray::SafeDownCast(grid->GetCellData()->GetArray("MATYPE"));
    auto *groupArr = vtkStringArray::SafeDownCast(grid->GetCellData()->GetAbstractArray("GroupName"));

    QMap<QString, int> counter;
    const vtkIdType nCells = grid->GetNumberOfCells();
    for (vtkIdType cid = 0; cid < nCells; ++cid) {
        const int matype = matypeArr ? matypeArr->GetValue(cid) : 0;
        const QString g = (groupArr && cid < groupArr->GetNumberOfValues())
                            ? QString::fromStdString(groupArr->GetValue(cid)).trimmed()
                            : QString();
        const QString key = QString("%1|%2").arg(matype).arg(g.isEmpty() ? QStringLiteral("(未命名)") : g);
        counter[key] += 1;
    }

    int row = 0;
    ui->tblGroups->setRowCount(counter.size());
    for (auto it = counter.begin(); it != counter.end(); ++it, ++row) {
        const QStringList parts = it.key().split('|');
        auto *itemMatype = new QTableWidgetItem(parts.value(0));
        auto *itemName = new QTableWidgetItem(parts.value(1));
        auto *itemCount = new QTableWidgetItem(QString::number(it.value()));

        itemMatype->setTextAlignment(Qt::AlignCenter);
        itemCount->setTextAlignment(Qt::AlignCenter);

        ui->tblGroups->setItem(row, 0, itemMatype);
        ui->tblGroups->setItem(row, 1, itemName);
        ui->tblGroups->setItem(row, 2, itemCount);
    }

    ui->tblGroups->resizeColumnsToContents();
}

void MeshGroupingWidget::updateRangePreview(bool forceOn)
{
    if (!forceOn && !ui->chkShowRangeBox->isChecked()) {
        clearRangePreview(false);
        return;
    }

    auto *visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual) return;

    double xmin = ui->dsXMin->value(), xmax = ui->dsXMax->value();
    double ymin = ui->dsYMin->value(), ymax = ui->dsYMax->value();
    double zmin = ui->dsZMin->value(), zmax = ui->dsZMax->value();

    if (xmin > xmax) std::swap(xmin, xmax);
    if (ymin > ymax) std::swap(ymin, ymax);
    if (zmin > zmax) std::swap(zmin, zmax);

    vtkNew<vtkCubeSource> cube;
    cube->SetBounds(xmin, xmax, ymin, ymax, zmin, zmax);

    vtkNew<vtkOutlineFilter> outline;
    outline->SetInputConnection(cube->GetOutputPort());

    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputConnection(outline->GetOutputPort());

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->GetProperty()->SetColor(1.0, 0.2, 0.2);
    actor->GetProperty()->SetLineWidth(2.0);

    visual->removeProp(kRangePreviewPropId, false);
    visual->addProp(kRangePreviewPropId, actor,
                    QStringLiteral("网格"), QStringLiteral("分组范围框"), false, true);
    m_rangePreviewActor = actor;
}

void MeshGroupingWidget::clearRangePreview(bool renderNow)
{
    auto *visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual) return;
    visual->removeProp(kRangePreviewPropId, renderNow);
    m_rangePreviewActor = nullptr;
}
