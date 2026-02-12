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
#include <cstring>
#include <limits>
#include <QMessageBox>
#include <QTableWidgetItem>

#include <vtkActor.h>
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkIdList.h>
#include <vtkConnectivityFilter.h>
#include <vtkDataSetSurfaceFilter.h>
#include <vtkIdTypeArray.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkPolyDataNormals.h>
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
    connect(ui->btnAutoBoundaryGrouping, &QPushButton::clicked, this, &MeshGroupingWidget::onAutoDetectBoundaries);
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
    m_hasAutoBoundaryGroups = false;
    m_autoBoundaryPickMap = MshPickMap();
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

void MeshGroupingWidget::onAutoDetectBoundaries()
{
    detectBoundaryGroupsBySurfacePatches();
    refreshBoundaryGroupList();
}

void MeshGroupingWidget::detectBoundaryGroupsBySurfacePatches()
{
    auto *meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshService) {
        QMessageBox::warning(this, "边界自动分组", "MeshModuleControllerService 不可用");
        return;
    }

    vtkSmartPointer<vtkUnstructuredGrid> grid = meshService->getVtkUnstructuredGridSolid();
    if (!grid || grid->GetNumberOfCells() <= 0) {
        grid = meshService->getVtkUnstructuredGrid();
    }
    if (!grid || grid->GetNumberOfCells() <= 0) {
        QMessageBox::warning(this, "边界自动分组", "当前没有可识别的体网格。");
        return;
    }

    vtkNew<vtkDataSetSurfaceFilter> surfaceFilter;
    surfaceFilter->SetInputData(grid);
    surfaceFilter->PassThroughPointIdsOn();
    surfaceFilter->PassThroughCellIdsOn();
    surfaceFilter->Update();

    // CAE 常规做法：先按特征角在锐边处分裂，再做连通域。
    // 这样立方体不会被识别为“一个外壳”，而是拆成多个边界面片。
    vtkNew<vtkPolyDataNormals> normals;
    normals->SetInputConnection(surfaceFilter->GetOutputPort());
    normals->ComputePointNormalsOff();
    normals->ComputeCellNormalsOn();
    normals->SplittingOn();
    normals->SetFeatureAngle(30.0); // 立方体(90°折角)会被稳定分裂
    normals->ConsistencyOn();
    normals->AutoOrientNormalsOn();
    normals->Update();

    vtkNew<vtkConnectivityFilter> conn;
    conn->SetInputConnection(normals->GetOutputPort());
    conn->SetExtractionModeToAllRegions();
    conn->ColorRegionsOn();
    conn->Update();

    vtkPolyData *poly = vtkPolyData::SafeDownCast(conn->GetOutput());
    if (!poly || poly->GetNumberOfCells() <= 0) {
        QMessageBox::warning(this, "边界自动分组", "未识别到外边界面。\n请确认导入的是体网格。");
        return;
    }

    auto *regionArr = vtkIntArray::SafeDownCast(poly->GetCellData()->GetArray("RegionId"));
    auto *origPointIds = vtkIdTypeArray::SafeDownCast(poly->GetPointData()->GetArray("vtkOriginalPointIds"));

    auto collectPointsFromPoly = [&](vtkPolyData* p, int regionId, QHash<int, QSet<int>>& out) {
        if (!p) return;
        auto *pointIds = vtkIdTypeArray::SafeDownCast(p->GetPointData()->GetArray("vtkOriginalPointIds"));
        const vtkIdType nCellsLocal = p->GetNumberOfCells();
        for (vtkIdType cid = 0; cid < nCellsLocal; ++cid) {
            vtkCell *cell = p->GetCell(cid);
            if (!cell) continue;
            vtkIdList *ids = cell->GetPointIds();
            for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i) {
                const vtkIdType surfPid = ids->GetId(i);
                int originalPid = static_cast<int>(surfPid);
                if (pointIds && surfPid < pointIds->GetNumberOfValues()) {
                    const vtkIdType oid = pointIds->GetValue(surfPid);
                    if (oid >= 0 && oid <= std::numeric_limits<int>::max()) {
                        originalPid = static_cast<int>(oid);
                    }
                }
                out[regionId].insert(originalPid);
            }
        }
    };

    auto packSortedFace = [](vtkIdList* ids)->QByteArray {
        QVector<int> v;
        if (!ids) return QByteArray();
        v.reserve((int)ids->GetNumberOfIds());
        for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i) {
            const vtkIdType pid = ids->GetId(i);
            if (pid < 0 || pid > std::numeric_limits<int>::max()) return QByteArray();
            v.push_back((int)pid);
        }
        std::sort(v.begin(), v.end());
        QByteArray k;
        k.resize((int)v.size() * (int)sizeof(int));
        if (!v.isEmpty()) std::memcpy(k.data(), v.constData(), (size_t)k.size());
        return k;
    };

    QHash<QByteArray, qint64> faceKeyIndex;
    const vtkIdType nVolCells = grid->GetNumberOfCells();
    for (vtkIdType cid = 0; cid < nVolCells; ++cid) {
        vtkCell* volCell = grid->GetCell(cid);
        if (!volCell) continue;
        const int nFaces = volCell->GetNumberOfFaces();
        for (int fi = 0; fi < nFaces; ++fi) {
            vtkCell* face = volCell->GetFace(fi);
            if (!face) continue;
            QByteArray key = packSortedFace(face->GetPointIds());
            if (key.isEmpty()) continue;
            const qint64 faceKey = (qint64(cid) << 4) | (qint64(fi) & 0xF);
            faceKeyIndex.insert(key, faceKey);
        }
    }

    QHash<int, QSet<int>> regionToPointIds;
    QHash<int, QVector<qint64>> regionToFaceKeys;
    if (regionArr) {
        const vtkIdType nCells = poly->GetNumberOfCells();
        for (vtkIdType cid = 0; cid < nCells; ++cid) {
            const int region = regionArr->GetValue(cid);
            vtkCell *cell = poly->GetCell(cid);
            if (!cell) continue;
            vtkIdList *ids = cell->GetPointIds();
            QByteArray key = packSortedFace(ids);
            auto hit = faceKeyIndex.find(key);
            if (hit != faceKeyIndex.end()) regionToFaceKeys[region].push_back(hit.value());
            for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i) {
                const vtkIdType surfPid = ids->GetId(i);
                int originalPid = static_cast<int>(surfPid);
                if (origPointIds && surfPid < origPointIds->GetNumberOfValues()) {
                    const vtkIdType oid = origPointIds->GetValue(surfPid);
                    if (oid >= 0 && oid <= std::numeric_limits<int>::max()) {
                        originalPid = static_cast<int>(oid);
                    }
                }
                regionToPointIds[region].insert(originalPid);
            }
        }
    } else {
        // 某些 VTK 版本/管线不会输出 RegionId：退化为按指定连通域逐个提取。
        const int regionCount = conn->GetNumberOfExtractedRegions();
        for (int r = 0; r < regionCount; ++r) {
            vtkNew<vtkConnectivityFilter> byRegion;
            byRegion->SetInputData(vtkPolyData::SafeDownCast(normals->GetOutput()));
            byRegion->SetExtractionModeToSpecifiedRegions();
            byRegion->AddSpecifiedRegion(r);
            byRegion->Update();

            vtkPolyData *regionPoly = vtkPolyData::SafeDownCast(byRegion->GetOutput());
            collectPointsFromPoly(regionPoly, r, regionToPointIds);
            if (regionPoly) {
                const vtkIdType nRC = regionPoly->GetNumberOfCells();
                for (vtkIdType rcid = 0; rcid < nRC; ++rcid) {
                    vtkCell* surfCell = regionPoly->GetCell(rcid);
                    if (!surfCell) continue;
                    QByteArray key = packSortedFace(surfCell->GetPointIds());
                    if (key.isEmpty()) continue;
                    auto hit = faceKeyIndex.find(key);
                    if (hit != faceKeyIndex.end()) regionToFaceKeys[r].push_back(hit.value());
                }
            }
        }
    }

    if (regionToPointIds.isEmpty()) {
        QMessageBox::warning(this, "边界自动分组", "未识别到有效边界连通域。");
        return;
    }

    MshPickMap merged = meshService->mshPickMapSolid();

    QList<int> regions = regionToPointIds.keys();
    std::sort(regions.begin(), regions.end());

    int autoPhysId = 910001;
    int added = 0;
    for (int r : regions) {
        QVector<int> pts = regionToPointIds.value(r).values().toVector();
        if (pts.size() < 3) continue;
        std::sort(pts.begin(), pts.end());

        while (merged.physIdToName.contains(autoPhysId)) ++autoPhysId;

        merged.bndFacePhysToPointIds[autoPhysId] = pts;
        QVector<qint64> faceKeys = regionToFaceKeys.value(r);
        std::sort(faceKeys.begin(), faceKeys.end());
        faceKeys.erase(std::unique(faceKeys.begin(), faceKeys.end()), faceKeys.end());
        merged.bndFacePhysToFaceKeys[autoPhysId] = faceKeys;
        merged.physIdToDim[autoPhysId] = 2;
        merged.physIdToName[autoPhysId] = QString("AutoPatch_%1").arg(added + 1, 3, 10, QChar('0'));

        ++autoPhysId;
        ++added;
    }

    if (added == 0) {
        QMessageBox::warning(this, "边界自动分组", "识别结果为空（可能全是退化面）。");
        return;
    }

    m_autoBoundaryPickMap = merged;
    m_hasAutoBoundaryGroups = true;
    meshService->setMshPickMapSolid(merged);

    QMessageBox::information(this, "边界自动分组",
                             QString("已按外表面连通域自动识别 %1 个边界组。\n(CAE 常规做法：先提取外表面，再按连通域/特征角分块)").arg(added));
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

    const MshPickMap pick = m_hasAutoBoundaryGroups ? m_autoBoundaryPickMap : meshService->mshPickMapSolid();
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
