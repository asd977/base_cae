#include "MeshGroupingWidget.h"
#include "ui_MeshGroupingWidget.h"

#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/VisualWidgetService.h>
#include <struct/MeshMap.h>

#include <QMap>
#include <cmath>
#include <QSet>
#include <algorithm>
#include <QMessageBox>
#include <QTableWidgetItem>

#include <vtkActor.h>
#include <vtkCellData.h>
#include <vtkCubeSource.h>
#include <vtkIntArray.h>
#include <vtkNew.h>
#include <vtkOutlineFilter.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkStringArray.h>
#include <vtkUnstructuredGrid.h>

namespace {
const QString kRangePreviewPropId = "mesh.grouping.range.preview";
}

MeshGroupingWidget::MeshGroupingWidget(QWidget *parent)
    : BasePage("MeshGroupingWidget")
    , ui(new Ui::MeshGroupingWidget)
{
    ui->setupUi(this);

    ui->cmbTargetArray->clear();
    ui->cmbTargetArray->addItem("材料分组(MATYPE)", "MATYPE");
    ui->cmbTargetArray->addItem("填筑层分组(LAYER)", "LAYER");
    ui->cmbRangeTargetArray->clear();
    ui->cmbRangeTargetArray->addItem("材料分组(MATYPE)", "MATYPE");
    ui->cmbRangeTargetArray->addItem("填筑层分组(LAYER)", "LAYER");

    connect(ui->btnAutoGrouping, &QPushButton::clicked, this, &MeshGroupingWidget::onAutoGrouping);
    connect(ui->btnRangeGrouping, &QPushButton::clicked, this, &MeshGroupingWidget::onRangeGrouping);
    connect(ui->btnRefreshGroups, &QPushButton::clicked, this, &MeshGroupingWidget::onRefreshGroups);
    connect(ui->chkShowRangeBox, &QCheckBox::toggled, this, &MeshGroupingWidget::onToggleRangePreview);
    connect(ui->cmbTargetArray, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int idx){ ui->cmbRangeTargetArray->setCurrentIndex(idx); });
    connect(ui->cmbRangeTargetArray, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int idx){ ui->cmbTargetArray->setCurrentIndex(idx); });

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

    initializeRangeByGridBounds(false);
    refreshGroupList();
    refreshBoundaryGroupList();
}

MeshGroupingWidget::~MeshGroupingWidget()
{
    clearRangePreview(false);
    delete ui;
}

void MeshGroupingWidget::beforeLoad()
{
    initializeRangeByGridBounds(false);
    refreshGroupList();
    refreshBoundaryGroupList();
    if (ui->chkShowRangeBox->isChecked()) {
        updateRangePreview(true);
    }
}

void MeshGroupingWidget::beforeUnload()
{
    clearRangePreview();
}

QString MeshGroupingWidget::currentTargetArrayName() const
{
    return ui->cmbRangeTargetArray->currentData().toString().trimmed();
}

void MeshGroupingWidget::onAutoGrouping()
{
    auto *meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshService) {
        QMessageBox::warning(this, "网格分组", "MeshModuleControllerService 不可用");
        return;
    }

    const QString targetArray = ui->cmbTargetArray->currentData().toString().trimmed();
    QString prefix = targetArray == "LAYER" ? "LayerGroup" : "MaterialGroup";

    QString err;
    if (!meshService->autoGroupingBySpatialBins(ui->spAutoGroupCount->value(),
                                                targetArray,
                                                ui->spStartValue->value(),
                                                prefix,
                                                &err)) {
        QMessageBox::warning(this, "自动分组失败", err.isEmpty() ? "自动分组失败" : err);
        return;
    }

    refreshGroupList();
    QMessageBox::information(this, "自动分组", "自动分组完成。目标字段已更新。\n如需边界批量选，请查看“边界组”页签。");
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
            currentTargetArrayName(),
            ui->spTargetValue->value(),
            groupName, &err)) {
        QMessageBox::warning(this, "范围分组失败", err.isEmpty() ? "范围分组失败" : err);
        return;
    }

    refreshGroupList();
    QMessageBox::information(this, "范围分组", "范围分组完成，命中单元已更新。");
}

void MeshGroupingWidget::onRefreshGroups()
{
    initializeRangeByGridBounds(true);
    refreshGroupList();
    refreshBoundaryGroupList();
}

void MeshGroupingWidget::initializeRangeByGridBounds(bool force)
{
    auto *meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshService) return;

    vtkSmartPointer<vtkUnstructuredGrid> grid = meshService->getVtkUnstructuredGridSolid();
    if (!grid || grid->GetNumberOfCells() <= 0) {
        grid = meshService->getVtkUnstructuredGrid();
    }
    if (!grid || grid->GetNumberOfPoints() <= 0) return;

    const bool hasUserRange =
            std::abs(ui->dsXMin->value() - ui->dsXMax->value()) > 1e-12 ||
            std::abs(ui->dsYMin->value() - ui->dsYMax->value()) > 1e-12 ||
            std::abs(ui->dsZMin->value() - ui->dsZMax->value()) > 1e-12;
    if (hasUserRange && !force) return;

    double bounds[6] = {0,0,0,0,0,0};
    grid->GetBounds(bounds);
    ui->dsXMin->setValue(bounds[0]);
    ui->dsXMax->setValue(bounds[1]);
    ui->dsYMin->setValue(bounds[2]);
    ui->dsYMax->setValue(bounds[3]);
    ui->dsZMin->setValue(bounds[4]);
    ui->dsZMax->setValue(bounds[5]);
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
        ui->tblMatypeGroups->setRowCount(0);
        ui->tblLayerGroups->setRowCount(0);
        return;
    }

    vtkSmartPointer<vtkUnstructuredGrid> grid = meshService->getVtkUnstructuredGridSolid();
    if (!grid || grid->GetNumberOfCells() <= 0) {
        grid = meshService->getVtkUnstructuredGrid();
    }

    auto fillTable = [&](QTableWidget* table, const char* arrayName) {
        table->clearContents();
        table->setRowCount(0);
        if (!grid || grid->GetNumberOfCells() <= 0) return;

        auto *arr = vtkIntArray::SafeDownCast(grid->GetCellData()->GetArray(arrayName));
        auto *groupArr = vtkStringArray::SafeDownCast(grid->GetCellData()->GetAbstractArray("GroupName"));

        QMap<QString, int> counter;
        const vtkIdType nCells = grid->GetNumberOfCells();
        for (vtkIdType cid = 0; cid < nCells; ++cid) {
            const int v = arr ? arr->GetValue(cid) : 0;
            const QString g = (groupArr && cid < groupArr->GetNumberOfValues())
                                ? QString::fromStdString(groupArr->GetValue(cid)).trimmed()
                                : QString();
            const QString key = QString("%1|%2").arg(v).arg(g.isEmpty() ? "(未命名)" : g);
            counter[key] += 1;
        }

        table->setRowCount(counter.size());
        int row = 0;
        for (auto it = counter.begin(); it != counter.end(); ++it, ++row) {
            const QStringList parts = it.key().split('|');
            auto *itemVal = new QTableWidgetItem(parts.value(0));
            auto *itemName = new QTableWidgetItem(parts.value(1));
            auto *itemCount = new QTableWidgetItem(QString::number(it.value()));
            itemVal->setTextAlignment(Qt::AlignCenter);
            itemCount->setTextAlignment(Qt::AlignCenter);
            table->setItem(row, 0, itemVal);
            table->setItem(row, 1, itemName);
            table->setItem(row, 2, itemCount);
        }
        table->resizeColumnsToContents();
    };

    fillTable(ui->tblMatypeGroups, "MATYPE");
    fillTable(ui->tblLayerGroups, "LAYER");
}

void MeshGroupingWidget::refreshBoundaryGroupList()
{
    auto *meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    ui->tblBoundaryGroups->clearContents();
    ui->tblBoundaryGroups->setRowCount(0);
    if (!meshService) return;

    MshPickMap pick = meshService->mshPickMapSolid();

    auto ensureBoundaryFallback = [&]() {
        const bool hasBoundary = !pick.bndPointPhysToPointIds.isEmpty()
                || !pick.bndEdgePhysToPointIds.isEmpty()
                || !pick.bndFacePhysToPointIds.isEmpty();
        if (hasBoundary) return;

        vtkSmartPointer<vtkUnstructuredGrid> grid = meshService->getVtkUnstructuredGridSolid();
        if (!grid || grid->GetNumberOfPoints() <= 0) {
            grid = meshService->getVtkUnstructuredGrid();
        }
        if (!grid || grid->GetNumberOfPoints() <= 0) return;

        double b[6] = {0,0,0,0,0,0};
        grid->GetBounds(b);
        const double spanX = std::max(std::abs(b[1] - b[0]), 1e-12);
        const double spanY = std::max(std::abs(b[3] - b[2]), 1e-12);
        const double spanZ = std::max(std::abs(b[5] - b[4]), 1e-12);
        const double tolX = std::max(spanX * 1e-6, 1e-9);
        const double tolY = std::max(spanY * 1e-6, 1e-9);
        const double tolZ = std::max(spanZ * 1e-6, 1e-9);

        QHash<int, QSet<int>> physToPts;
        for (vtkIdType pid = 0; pid < grid->GetNumberOfPoints(); ++pid) {
            double p[3] = {0,0,0};
            grid->GetPoint(pid, p);
            if (std::abs(p[0] - b[0]) <= tolX) physToPts[900001].insert((int)pid);
            if (std::abs(p[0] - b[1]) <= tolX) physToPts[900002].insert((int)pid);
            if (std::abs(p[1] - b[2]) <= tolY) physToPts[900003].insert((int)pid);
            if (std::abs(p[1] - b[3]) <= tolY) physToPts[900004].insert((int)pid);
            if (std::abs(p[2] - b[4]) <= tolZ) physToPts[900005].insert((int)pid);
            if (std::abs(p[2] - b[5]) <= tolZ) physToPts[900006].insert((int)pid);
        }

        const QHash<int, QString> nameMap {
            {900001, "Auto_XMin"}, {900002, "Auto_XMax"},
            {900003, "Auto_YMin"}, {900004, "Auto_YMax"},
            {900005, "Auto_ZMin"}, {900006, "Auto_ZMax"},
        };

        for (auto it = physToPts.begin(); it != physToPts.end(); ++it) {
            if (it.value().isEmpty()) continue;
            QVector<int> pts;
            pts.reserve(it.value().size());
            for (int v : it.value()) pts.push_back(v);
            std::sort(pts.begin(), pts.end());
            pick.bndFacePhysToPointIds[it.key()] = pts;
            pick.physIdToDim[it.key()] = 2;
            pick.physIdToName[it.key()] = nameMap.value(it.key(), QString("Auto_Boundary_%1").arg(it.key()));
        }
    };
    ensureBoundaryFallback();
    QList<int> physIds = pick.physIdToName.keys();
    std::sort(physIds.begin(), physIds.end());

    ui->tblBoundaryGroups->setRowCount(physIds.size());
    int row = 0;
    for (int physId : physIds) {
        const int dim = pick.physIdToDim.value(physId, -1);
        if (dim < 0 || dim > 2) continue;

        QString dimName = (dim == 0) ? "点边界" : (dim == 1 ? "线边界" : "面边界");
        int count = 0;
        if (dim == 0) count = pick.bndPointPhysToPointIds.value(physId).size();
        if (dim == 1) count = pick.bndEdgePhysToPointIds.value(physId).size();
        if (dim == 2) count = pick.bndFacePhysToPointIds.value(physId).size();

        ui->tblBoundaryGroups->setItem(row, 0, new QTableWidgetItem(QString::number(physId)));
        ui->tblBoundaryGroups->setItem(row, 1, new QTableWidgetItem(pick.physIdToName.value(physId)));
        ui->tblBoundaryGroups->setItem(row, 2, new QTableWidgetItem(dimName));
        ui->tblBoundaryGroups->setItem(row, 3, new QTableWidgetItem(QString::number(count)));
        ++row;
    }
    ui->tblBoundaryGroups->setRowCount(row);
    ui->tblBoundaryGroups->resizeColumnsToContents();
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
    actor->GetProperty()->LightingOff();

    if (std::abs(xmax - xmin) < 1e-12 || std::abs(ymax - ymin) < 1e-12 || std::abs(zmax - zmin) < 1e-12) {
        auto *meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
        vtkSmartPointer<vtkUnstructuredGrid> grid = meshService ? meshService->getVtkUnstructuredGridSolid() : nullptr;
        if ((!grid || grid->GetNumberOfPoints() <= 0) && meshService) {
            grid = meshService->getVtkUnstructuredGrid();
        }
        if (grid && grid->GetNumberOfPoints() > 0) {
            double gb[6] = {0,0,0,0,0,0};
            grid->GetBounds(gb);
            const double eps = std::max({std::abs(gb[1]-gb[0]), std::abs(gb[3]-gb[2]), std::abs(gb[5]-gb[4]), 1.0}) * 1e-4;
            if (std::abs(xmax - xmin) < 1e-12) { xmin -= eps; xmax += eps; }
            if (std::abs(ymax - ymin) < 1e-12) { ymin -= eps; ymax += eps; }
            if (std::abs(zmax - zmin) < 1e-12) { zmin -= eps; zmax += eps; }
            cube->SetBounds(xmin, xmax, ymin, ymax, zmin, zmax);
        }
    }

    visual->removeProp(kRangePreviewPropId, false);
    visual->addProp(kRangePreviewPropId, actor, "网格", "分组范围框", false, true);
    m_rangePreviewActor = actor;
}

void MeshGroupingWidget::clearRangePreview(bool renderNow)
{
    auto *visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual) return;
    visual->removeProp(kRangePreviewPropId, renderNow);
    m_rangePreviewActor = nullptr;
}
