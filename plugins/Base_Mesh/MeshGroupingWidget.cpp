#include "MeshGroupingWidget.h"

#include <ServiceRegistry.h>
#include <PickManager.h>
#include <services/MeshModuleControllerService.h>
#include <services/VisualWidgetService.h>
#include <struct/MeshMap.h>
#include <vtkActor.h>
#include <vtkExtractCells.h>
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkConnectivityFilter.h>
#include <vtkCubeSource.h>
#include <vtkDataSetSurfaceFilter.h>
#include <vtkIdList.h>
#include <vtkIdTypeArray.h>
#include <vtkIntArray.h>
#include <vtkNew.h>
#include <vtkOutlineFilter.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkMapper.h>
#include <vtkPolyDataNormals.h>
#include <vtkProperty.h>
#include <vtkStringArray.h>
#include <vtkUnstructuredGrid.h>

#include <QMap>
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QGroupBox>
#include <QTabWidget>
#include <QCheckBox>
#include <QAbstractItemView>
#include <QItemSelectionModel>
#include <QModelIndex>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QSignalBlocker>
#include <QLabel>
#include <QMessageBox>
#include <QSet>
#include <QTableWidgetItem>
#include <algorithm>
#include <cmath>
#include <limits>

#include "ui_MeshGroupingWidget.h"

namespace
{
const QString kRangePreviewPropId = "mesh.grouping.range.preview";
const QString kManualPreviewPropId = "mesh.grouping.selection.preview";
const QString kPickDomain = "MeshGroupingManual";
const QString kPickUuid = "cells";
constexpr int kMaxPreviewCells = 100000;
constexpr int FIRST_AUTO_MATYPE = 3;
constexpr int MAX_SOLVER_MATYPE = 300;

static bool physicalTagExists(const MshPickMap& pick, int physId)
{
    if (physId <= 0)
        return false;

    for (auto it = pick.tagSets.constBegin(); it != pick.tagSets.constEnd(); ++it)
    {
        const MeshTagKey& key = it.key();
        if (key.category == QStringLiteral("Physical") && key.id == physId)
            return true;
    }
    return false;
}

static QVector<MeshIdType> toMeshIds(const QVector<int>& values)
{
    QVector<MeshIdType> out;
    out.reserve(values.size());
    for (int v : values)
        out.push_back(static_cast<MeshIdType>(v));
    return out;
}

static QVector<MeshIdType> toMeshIds(const QVector<qint64>& values)
{
    QVector<MeshIdType> out;
    out.reserve(values.size());
    for (qint64 v : values)
        out.push_back(static_cast<MeshIdType>(v));
    return out;
}

static void insertPhysicalBoundarySet(MshPickMap& pick,
                                      int dim,
                                      int physId,
                                      const QString& name,
                                      const QString& source,
                                      const QVector<int>& pointIds,
                                      const QVector<qint64>& edgeKeys = {},
                                      const QVector<qint64>& faceKeys = {})
{
    if (physId <= 0 || dim < 0 || dim > 2)
        return;

    const MeshTagKey key{QStringLiteral("Physical"), dim, physId};

    MeshTagSet set;
    set.key = key;
    set.name = name;
    set.source = source;
    set.pointIds = toMeshIds(pointIds);
    set.edgeKeys = toMeshIds(edgeKeys);
    set.faceKeys = toMeshIds(faceKeys);

    pick.tagSets.insert(key, set);

    for (int pid : pointIds)
    {
        if (pid < 0)
            continue;

        QVector<MeshTagKey>& tags = pick.pointIdToBoundaryTags[static_cast<MeshIdType>(pid)];
        if (!tags.contains(key))
            tags.push_back(key);
    }
}
}

MeshGroupingWidget::MeshGroupingWidget(QWidget* parent)
    : BasePage("MeshGroupingWidget"), ui(new Ui::MeshGroupingWidget)
{
    ui->setupUi(this);

    ui->cmbTargetArray->clear();
    ui->cmbTargetArray->addItem("材料分组", "MATYPE");
    ui->cmbTargetArray->addItem("填筑层分组", "LAYER");
    ui->cmbRangeTargetArray->clear();
    ui->cmbRangeTargetArray->addItem("材料分组", "MATYPE");
    ui->cmbRangeTargetArray->addItem("填筑层分组", "LAYER");


    auto syncMatypeNumberBounds = [this]()
    {
        const bool autoTargetIsMatype =
            ui->cmbTargetArray->currentData().toString().compare("MATYPE", Qt::CaseInsensitive) == 0;
        ui->spStartValue->setMinimum(autoTargetIsMatype ? FIRST_AUTO_MATYPE : 1);
        ui->spStartValue->setMaximum(autoTargetIsMatype ? MAX_SOLVER_MATYPE : 1000000);
        if (autoTargetIsMatype && ui->spStartValue->value() < FIRST_AUTO_MATYPE)
            ui->spStartValue->setValue(FIRST_AUTO_MATYPE);

        const bool rangeTargetIsMatype =
            ui->cmbRangeTargetArray->currentData().toString().compare("MATYPE", Qt::CaseInsensitive) == 0;
        ui->spTargetValue->setMinimum(rangeTargetIsMatype ? FIRST_AUTO_MATYPE : 1);
        ui->spTargetValue->setMaximum(rangeTargetIsMatype ? MAX_SOLVER_MATYPE : 1000000);
        if (rangeTargetIsMatype && ui->spTargetValue->value() < FIRST_AUTO_MATYPE)
            ui->spTargetValue->setValue(FIRST_AUTO_MATYPE);
    };

    connect(ui->btnAutoGrouping, &QPushButton::clicked, this, &MeshGroupingWidget::onAutoGrouping);
    connect(ui->btnRangeGrouping, &QPushButton::clicked, this,
            &MeshGroupingWidget::onRangeGrouping);
    connect(ui->btnRefreshGroups, &QPushButton::clicked, this,
            &MeshGroupingWidget::onRefreshGroups);
    connect(ui->btnAutoBoundaryGrouping, &QPushButton::clicked, this,
            &MeshGroupingWidget::onAutoDetectBoundaries);
    connect(ui->chkShowRangeBox, &QCheckBox::toggled, this,
            &MeshGroupingWidget::onToggleRangePreview);
    connect(ui->cmbTargetArray, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this, syncMatypeNumberBounds](int idx)
            {
                ui->cmbRangeTargetArray->setCurrentIndex(idx);
                syncMatypeNumberBounds();
            });
    connect(ui->cmbRangeTargetArray, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this, syncMatypeNumberBounds](int idx)
            {
                ui->cmbTargetArray->setCurrentIndex(idx);
                syncMatypeNumberBounds();
            });
    syncMatypeNumberBounds();

    auto bindRangeChanged = [this](QDoubleSpinBox* s)
    {
        connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                &MeshGroupingWidget::onRangeValueChanged);
    };
    bindRangeChanged(ui->dsXMin);
    bindRangeChanged(ui->dsXMax);
    bindRangeChanged(ui->dsYMin);
    bindRangeChanged(ui->dsYMax);
    bindRangeChanged(ui->dsZMin);
    bindRangeChanged(ui->dsZMax);

    buildManualGroupingControls();
    connect(&PickManager::instance(), &PickManager::selectionChanged, this,
            [this](const QString& domain, const QString& uuid, PickMode mode, GridKind kind) {
                if (m_activeManualPicking && !m_ignoreFaceSignal &&
                    domain == kPickDomain && uuid == kPickUuid &&
                    mode == PickMode::Face && kind == GridKind::Solid)
                    onPickedFacesChanged();
            });
    initializeRangeByGridBounds(false);
    refreshGroupList();
    refreshBoundaryGroupList();
    refreshSelectionSets();
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
    m_manualCellIds.clear();
    m_lastFaceKeys.clear();
    refreshSelectionSets();
    initializeRangeByGridBounds(false);
    refreshGroupList();
    refreshBoundaryGroupList();
    PickManager::instance().updateStructure();
    m_ignoreFaceSignal = true;
    PickManager::instance().activate(kPickDomain, kPickUuid, PickMode::Face);
    PickManager::instance().setBoxSelectEnabled(kPickDomain, kPickUuid, true);
    PickManager::instance().setClickPickEnabled(kPickDomain, kPickUuid, true);
    PickManager::instance().clearSelection(kPickDomain, kPickUuid, PickMode::Face);
    m_ignoreFaceSignal = false;
    m_activeManualPicking = true;
    syncGroupTab();
    if (ui->chkShowRangeBox->isChecked())
    {
        updateRangePreview(true);
    }
}

void MeshGroupingWidget::beforeUnload()
{
    m_activeManualPicking = false;
    m_ignoreFaceSignal = true;
    PickManager::instance().deactivate(kPickDomain, kPickUuid, PickMode::Face);
    m_ignoreFaceSignal = false;
    if (auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>())
        visual->removeProp(kManualPreviewPropId, false);
    m_manualPreviewActor = nullptr;
    clearRangePreview();
}

QString MeshGroupingWidget::currentTargetArrayName() const
{
    return ui->cmbRangeTargetArray->currentData().toString().trimmed();
}

QString MeshGroupingWidget::groupFieldForTab(int tab) const
{
    const QWidget* page = ui->tabGroupingInfo->widget(tab);
    if (page == ui->tabLayer) return QStringLiteral("LAYER");
    return m_primaryGroupCategory;
}

bool MeshGroupingWidget::isSelectionTab() const
{
    return ui->tabGroupingInfo->currentWidget() == m_selectionSetsPage;
}

void MeshGroupingWidget::onAutoGrouping()
{
    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshService)
    {
        QMessageBox::warning(this, "网格分组", "网格管理功能尚未就绪，请稍后重试。");
        return;
    }

    const QString targetArray = ui->cmbTargetArray->currentData().toString().trimmed();
    if (!m_dualSolverGroups && QMessageBox::question(this, QStringLiteral("重建原始区域"),
        QStringLiteral("按空间位置重新划分原始网格区域？这会替换当前区域分组，已有材料/开挖/坝体区域引用不会自动迁移；MATYPE 和 LAYER 不变。"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    QString prefix = targetArray == "LAYER" ? "LayerGroup"
        : (targetArray == "MATYPE" ? "MaterialGroup" : "RegionGroup");

    QString err;
    if (!meshService->autoGroupingBySpatialBins(ui->spAutoGroupCount->value(), targetArray,
                                                ui->spStartValue->value(), prefix, &err))
    {
        QMessageBox::warning(this, "自动分组失败", err.isEmpty() ? "自动分组失败" : err);
        return;
    }

    refreshGroupList();
    QMessageBox::information(this, "自动分组", "划分完成");
}

void MeshGroupingWidget::onRangeGrouping()
{
    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshService)
    {
        QMessageBox::warning(this, "网格分组", "网格管理功能尚未就绪，请稍后重试。");
        return;
    }

    const QString groupName = ui->edtGroupName->text().trimmed();
    if (groupName.isEmpty())
    {
        QMessageBox::warning(this, "参数错误", "请输入分组名称。");
        return;
    }

    QString err;
    if (!meshService->groupByCoordinateRange(
            ui->dsXMin->value(), ui->dsXMax->value(), ui->dsYMin->value(), ui->dsYMax->value(),
            ui->dsZMin->value(), ui->dsZMax->value(), currentTargetArrayName(),
            ui->spTargetValue->value(), groupName, &err))
    {
        QMessageBox::warning(this, "范围分组失败", err.isEmpty() ? "范围分组失败" : err);
        return;
    }

    refreshGroupList();
    QMessageBox::information(this, "范围分组", "划分完成");
}

void MeshGroupingWidget::onRefreshGroups()
{
    initializeRangeByGridBounds(true);
    refreshGroupList();
    refreshBoundaryGroupList();
    refreshSelectionSets();
}

void MeshGroupingWidget::onAutoDetectBoundaries()
{
    detectBoundaryGroupsBySurfacePatches();
    refreshBoundaryGroupList();
}

void MeshGroupingWidget::detectBoundaryGroupsBySurfacePatches()
{
    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshService)
    {
        QMessageBox::warning(this, "边界自动分组", "网格管理功能尚未就绪，请稍后重试。");
        return;
    }

    vtkSmartPointer<vtkUnstructuredGrid> grid = meshService->getVtkUnstructuredGridSolid();
    if (!grid || grid->GetNumberOfCells() <= 0)
    {
        grid = meshService->getVtkUnstructuredGrid();
    }
    if (!grid || grid->GetNumberOfCells() <= 0)
    {
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
    // 收紧特征角，避免明显不共面的面片被归到同一边界组。
    // 仍是常规做法：按特征角分裂 + 连通域。
    normals->SetFeatureAngle(12.0);
    normals->ConsistencyOn();
    normals->AutoOrientNormalsOn();
    normals->Update();

    vtkNew<vtkConnectivityFilter> conn;
    conn->SetInputConnection(normals->GetOutputPort());
    conn->SetExtractionModeToAllRegions();
    conn->ColorRegionsOn();
    conn->Update();

    vtkPolyData* poly = vtkPolyData::SafeDownCast(conn->GetOutput());
    if (!poly || poly->GetNumberOfCells() <= 0)
    {
        QMessageBox::warning(this, "边界自动分组", "未识别到外边界面。\n请确认导入的是体网格。");
        return;
    }

    auto* regionArr = vtkIntArray::SafeDownCast(poly->GetCellData()->GetArray("RegionId"));
    auto* origPointIds =
        vtkIdTypeArray::SafeDownCast(poly->GetPointData()->GetArray("vtkOriginalPointIds"));

    auto collectPointsFromPoly = [&](vtkPolyData* p, int regionId, QHash<int, QSet<int>>& out)
    {
        if (!p)
            return;
        auto* pointIds =
            vtkIdTypeArray::SafeDownCast(p->GetPointData()->GetArray("vtkOriginalPointIds"));
        const vtkIdType nCellsLocal = p->GetNumberOfCells();
        for (vtkIdType cid = 0; cid < nCellsLocal; ++cid)
        {
            vtkCell* cell = p->GetCell(cid);
            if (!cell)
                continue;
            vtkIdList* ids = cell->GetPointIds();
            for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
            {
                const vtkIdType surfPid = ids->GetId(i);
                int originalPid = static_cast<int>(surfPid);
                if (pointIds && surfPid < pointIds->GetNumberOfValues())
                {
                    const vtkIdType oid = pointIds->GetValue(surfPid);
                    if (oid >= 0 && oid <= std::numeric_limits<int>::max())
                    {
                        originalPid = static_cast<int>(oid);
                    }
                }
                out[regionId].insert(originalPid);
            }
        }
    };

    struct FaceEntry
    {
        qint64 key = -1;
        QVector<int> pointIds;
    };

    QVector<FaceEntry> allFaces;
    allFaces.reserve((int)grid->GetNumberOfCells() * 4);

    const vtkIdType nVolCells = grid->GetNumberOfCells();
    for (vtkIdType cid = 0; cid < nVolCells; ++cid)
    {
        vtkCell* volCell = grid->GetCell(cid);
        if (!volCell)
            continue;
        const int nFaces = volCell->GetNumberOfFaces();
        for (int fi = 0; fi < nFaces; ++fi)
        {
            vtkCell* face = volCell->GetFace(fi);
            if (!face)
                continue;
            vtkIdList* ids = face->GetPointIds();
            if (!ids || ids->GetNumberOfIds() < 3)
                continue;

            FaceEntry e;
            e.key = (qint64(cid) << 4) | (qint64(fi) & 0xF);
            e.pointIds.reserve((int)ids->GetNumberOfIds());
            for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
            {
                const vtkIdType pid = ids->GetId(i);
                if (pid < 0 || pid > std::numeric_limits<int>::max())
                    continue;
                e.pointIds.push_back((int)pid);
            }
            if (e.pointIds.size() >= 3)
                allFaces.push_back(e);
        }
    }

    QHash<int, QSet<int>> regionToPointIds;
    if (regionArr)
    {
        const vtkIdType nCells = poly->GetNumberOfCells();
        for (vtkIdType cid = 0; cid < nCells; ++cid)
        {
            const int region = regionArr->GetValue(cid);
            vtkCell* cell = poly->GetCell(cid);
            if (!cell)
                continue;
            vtkIdList* ids = cell->GetPointIds();
            for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
            {
                const vtkIdType surfPid = ids->GetId(i);
                int originalPid = static_cast<int>(surfPid);
                if (origPointIds && surfPid < origPointIds->GetNumberOfValues())
                {
                    const vtkIdType oid = origPointIds->GetValue(surfPid);
                    if (oid >= 0 && oid <= std::numeric_limits<int>::max())
                    {
                        originalPid = static_cast<int>(oid);
                    }
                }
                regionToPointIds[region].insert(originalPid);
            }
        }
    }
    else
    {
        // 某些 VTK 版本/管线不会输出 RegionId：退化为按指定连通域逐个提取。
        const int regionCount = conn->GetNumberOfExtractedRegions();
        for (int r = 0; r < regionCount; ++r)
        {
            vtkNew<vtkConnectivityFilter> byRegion;
            byRegion->SetInputData(vtkPolyData::SafeDownCast(normals->GetOutput()));
            byRegion->SetExtractionModeToSpecifiedRegions();
            byRegion->AddSpecifiedRegion(r);
            byRegion->Update();
            vtkPolyData* regionPoly = vtkPolyData::SafeDownCast(byRegion->GetOutput());
            collectPointsFromPoly(regionPoly, r, regionToPointIds);
        }
    }

    if (regionToPointIds.isEmpty())
    {
        QMessageBox::warning(this, "边界自动分组", "未识别到有效边界连通域。");
        return;
    }

    // 把每个体网格面唯一指派到最匹配的 region，避免重合/交界碎片被单独算一组。
    // 性能优化：由“遍历全部 region”改为“通过点反查候选 region”，复杂度显著降低。
    QHash<int, QVector<qint64>> regionToFaceKeys;
    QHash<int, QVector<int>> pointToRegions;
    for (auto it = regionToPointIds.constBegin(); it != regionToPointIds.constEnd(); ++it)
    {
        const int rid = it.key();
        for (int pid : it.value())
        {
            pointToRegions[pid].push_back(rid);
        }
    }

    for (const FaceEntry& f : allFaces)
    {
        QHash<int, int> hitCounter;
        for (int pid : f.pointIds)
        {
            auto pit = pointToRegions.constFind(pid);
            if (pit == pointToRegions.constEnd())
                continue;
            const QVector<int>& candidates = pit.value();
            for (int rid : candidates)
            {
                hitCounter[rid] += 1;
            }
        }

        int bestRegion = -1;
        int bestHit = -1;
        bool tie = false;
        for (auto it = hitCounter.constBegin(); it != hitCounter.constEnd(); ++it)
        {
            const int rid = it.key();
            const int hit = it.value();
            if (hit > bestHit)
            {
                bestHit = hit;
                bestRegion = rid;
                tie = false;
            }
            else if (hit == bestHit)
            {
                tie = true;
            }
        }

        if (bestRegion < 0 || tie)
            continue;
        if (bestHit != f.pointIds.size())
            continue; // 必须完整覆盖该面
        regionToFaceKeys[bestRegion].push_back(f.key);
    }

    MshPickMap merged = meshService->mshPickMapSolid();

    QList<int> regions = regionToFaceKeys.keys();
    std::sort(regions.begin(), regions.end());

    int autoPhysId = 910001;
    int added = 0;
    for (int r : regions)
    {
        QVector<qint64> faceKeys = regionToFaceKeys.value(r);
        std::sort(faceKeys.begin(), faceKeys.end());
        faceKeys.erase(std::unique(faceKeys.begin(), faceKeys.end()), faceKeys.end());
        if (faceKeys.isEmpty())
            continue;

        QSet<int> pointSet;
        for (qint64 fk : faceKeys)
        {
            const vtkIdType cid = vtkIdType((fk >> 4));
            const int fi = int(fk & 0xF);
            vtkCell* volCell = grid->GetCell(cid);
            if (!volCell || fi < 0 || fi >= volCell->GetNumberOfFaces())
                continue;
            vtkCell* face = volCell->GetFace(fi);
            if (!face)
                continue;
            vtkIdList* ids = face->GetPointIds();
            for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
            {
                const vtkIdType pid = ids->GetId(i);
                if (pid >= 0 && pid <= std::numeric_limits<int>::max())
                    pointSet.insert((int)pid);
            }
        }
        QVector<int> pts = pointSet.values().toVector();
        std::sort(pts.begin(), pts.end());
        if (pts.size() < 3)
            continue;

        while (physicalTagExists(merged, autoPhysId))
            ++autoPhysId;

        const QString baseName = QString("AutoPatch_%1").arg(added + 1, 3, 10, QChar('0'));

        insertPhysicalBoundarySet(merged,
                                  2,
                                  autoPhysId,
                                  baseName,
                                  QStringLiteral("MeshGroupingWidget"),
                                  pts,
                                  {},
                                  faceKeys);

        // 额外生成线/点边界组，便于边界条件页面直接按 dim=1/0 复用。
        QVector<qint64> edgeKeys;
        QSet<int> edgePointSet;
        for (qint64 fk : faceKeys)
        {
            const vtkIdType cid = vtkIdType((fk >> 4));
            const int fi = int(fk & 0xF);
            vtkCell* volCell = grid->GetCell(cid);
            if (!volCell || fi < 0 || fi >= volCell->GetNumberOfFaces())
                continue;
            vtkCell* face = volCell->GetFace(fi);
            if (!face)
                continue;

            for (int ei = 0; ei < face->GetNumberOfEdges(); ++ei)
            {
                vtkCell* fEdge = face->GetEdge(ei);
                if (!fEdge || fEdge->GetNumberOfPoints() < 2)
                    continue;
                vtkIdType a = fEdge->GetPointId(0), b = fEdge->GetPointId(1);
                if (a > b)
                    std::swap(a, b);

                for (int vei = 0; vei < volCell->GetNumberOfEdges(); ++vei)
                {
                    vtkCell* vEdge = volCell->GetEdge(vei);
                    if (!vEdge || vEdge->GetNumberOfPoints() < 2)
                        continue;
                    vtkIdType va = vEdge->GetPointId(0), vb = vEdge->GetPointId(1);
                    if (va > vb)
                        std::swap(va, vb);
                    if (va == a && vb == b)
                    {
                        edgeKeys.push_back((qint64(cid) << 4) | (qint64(vei) & 0xF));
                        if (va >= 0 && va <= std::numeric_limits<int>::max())
                            edgePointSet.insert((int)va);
                        if (vb >= 0 && vb <= std::numeric_limits<int>::max())
                            edgePointSet.insert((int)vb);
                        break;
                    }
                }
            }
        }
        std::sort(edgeKeys.begin(), edgeKeys.end());
        edgeKeys.erase(std::unique(edgeKeys.begin(), edgeKeys.end()), edgeKeys.end());

        int edgePhysId = autoPhysId + 100000;
        while (physicalTagExists(merged, edgePhysId))
            ++edgePhysId;
        if (!edgeKeys.isEmpty())
        {
            QVector<int> edgePts = edgePointSet.values().toVector();
            std::sort(edgePts.begin(), edgePts.end());

            insertPhysicalBoundarySet(merged,
                                      1,
                                      edgePhysId,
                                      baseName + "_Edge",
                                      QStringLiteral("MeshGroupingWidget"),
                                      edgePts,
                                      edgeKeys,
                                      {});
        }

        int pointPhysId = autoPhysId + 200000;
        while (physicalTagExists(merged, pointPhysId))
            ++pointPhysId;
        if (!pts.isEmpty())
        {
            insertPhysicalBoundarySet(merged,
                                      0,
                                      pointPhysId,
                                      baseName + "_Point",
                                      QStringLiteral("MeshGroupingWidget"),
                                      pts);
        }

        ++autoPhysId;
        ++added;
    }

    if (added == 0)
    {
        QMessageBox::warning(this, "边界自动分组", "识别结果为空（可能全是退化面）。");
        return;
    }

    m_autoBoundaryPickMap = merged;
    m_hasAutoBoundaryGroups = true;
    meshService->setMshPickMapSolid(merged);

    QMessageBox::information(this, "边界自动分组", "划分完成");
}

void MeshGroupingWidget::initializeRangeByGridBounds(bool force)
{
    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshService)
        return;

    vtkSmartPointer<vtkUnstructuredGrid> grid = meshService->getVtkUnstructuredGridSolid();
    if (!grid || grid->GetNumberOfCells() <= 0)
    {
        grid = meshService->getVtkUnstructuredGrid();
    }
    if (!grid || grid->GetNumberOfPoints() <= 0)
        return;

    const bool hasUserRange = std::abs(ui->dsXMin->value() - ui->dsXMax->value()) > 1e-12 ||
                              std::abs(ui->dsYMin->value() - ui->dsYMax->value()) > 1e-12 ||
                              std::abs(ui->dsZMin->value() - ui->dsZMax->value()) > 1e-12;
    if (hasUserRange && !force)
        return;

    double bounds[6] = {0, 0, 0, 0, 0, 0};
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
    if (on)
        updateRangePreview(true);
    else
        clearRangePreview();
}

void MeshGroupingWidget::onRangeValueChanged()
{
    if (ui->chkShowRangeBox->isChecked())
    {
        updateRangePreview(true);
    }
}

void MeshGroupingWidget::refreshGroupList()
{
    auto selectedIds = [](QTableWidget* table) {
        QSet<int> ids;
        if (table && table->selectionModel())
            for (const QModelIndex& index : table->selectionModel()->selectedRows())
                if (auto* item = table->item(index.row(), 0)) ids.insert(item->text().toInt());
        return ids;
    };
    const QSet<int> matPrior = selectedIds(ui->tblMatypeGroups);
    const QSet<int> layPrior = selectedIds(ui->tblLayerGroups);
    m_rebuildingGroups = true;
    QSignalBlocker matBlock(ui->tblMatypeGroups);
    QSignalBlocker layBlock(ui->tblLayerGroups);
    auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    ui->tblMatypeGroups->clearContents();
    ui->tblLayerGroups->clearContents();
    ui->tblMatypeGroups->setRowCount(0);
    ui->tblLayerGroups->setRowCount(0);
    if (!svc)
    {
        m_rebuildingGroups = false;
        return;
    }
    const MshPickMap map = svc->mshPickMapSolid();
    const QString materialCategory = svc->domainCategoryForUse(MeshDomainUse::MaterialRegion);
    const QString excavationCategory = svc->domainCategoryForUse(MeshDomainUse::ExcavationRegion);
    // Only native IIIN/DAT has two independent SOURCE groupings. For all
    // other meshes there is one editable original region category.
    const bool dual = materialCategory.compare(QStringLiteral("MATYPE"), Qt::CaseInsensitive) == 0
        && excavationCategory.compare(QStringLiteral("LAYER"), Qt::CaseInsensitive) == 0;
    const QString primary = materialCategory.isEmpty() ? QStringLiteral("MATYPE") : materialCategory;
    const bool groupingChanged = dual != m_dualSolverGroups
        || primary.compare(m_primaryGroupCategory, Qt::CaseInsensitive) != 0;
    m_dualSolverGroups = dual;
    m_primaryGroupCategory = primary;
    if (groupingChanged)
    {
        QSignalBlocker tabBlock(ui->tabGroupingInfo);
        QSignalBlocker autoBlock(ui->cmbTargetArray);
        QSignalBlocker rangeBlock(ui->cmbRangeTargetArray);
        const int layerTab = ui->tabGroupingInfo->indexOf(ui->tabLayer);
        if (dual && layerTab < 0)
            ui->tabGroupingInfo->insertTab(1, ui->tabLayer, QStringLiteral("施工/开挖层分组"));
        else if (!dual && layerTab >= 0)
            ui->tabGroupingInfo->removeTab(layerTab);
        ui->tabGroupingInfo->setTabText(0,
            dual ? QStringLiteral("材料分组") : QStringLiteral("网格区域"));
        ui->tblMatypeGroups->horizontalHeaderItem(0)->setText(
            dual ? QStringLiteral("材料编号") : QStringLiteral("区域编号"));
        ui->cmbTargetArray->clear();
        ui->cmbRangeTargetArray->clear();
        if (dual)
        {
            ui->cmbTargetArray->addItem(QStringLiteral("材料分组"), QStringLiteral("MATYPE"));
            ui->cmbTargetArray->addItem(QStringLiteral("施工/开挖层分组"), QStringLiteral("LAYER"));
            ui->cmbRangeTargetArray->addItem(QStringLiteral("材料分组"), QStringLiteral("MATYPE"));
            ui->cmbRangeTargetArray->addItem(QStringLiteral("施工/开挖层分组"), QStringLiteral("LAYER"));
        }
        else
        {
            ui->cmbTargetArray->addItem(QStringLiteral("网格区域"), primary);
            ui->cmbRangeTargetArray->addItem(QStringLiteral("网格区域"), primary);
        }
        ui->spStartValue->setMinimum(dual ? FIRST_AUTO_MATYPE : 1);
        ui->spStartValue->setMaximum(dual ? MAX_SOLVER_MATYPE : 1000000);
        ui->spTargetValue->setMinimum(dual ? FIRST_AUTO_MATYPE : 1);
        ui->spTargetValue->setMaximum(dual ? MAX_SOLVER_MATYPE : 1000000);
        if (dual)
        {
            ui->spStartValue->setValue(qMax(FIRST_AUTO_MATYPE, ui->spStartValue->value()));
            ui->spTargetValue->setValue(qMax(FIRST_AUTO_MATYPE, ui->spTargetValue->value()));
        }
        else
        {
            ui->spStartValue->setValue(1);
            ui->spTargetValue->setValue(1);
        }
        // Avoid changing solver MATYPE/LAYER merely to split the original mesh.
        ui->cmbTargetArray->setToolTip(dual ? QString()
            : QStringLiteral("修改原始网格分区，不更改计算材料编号和施工层号。"));
    }
    struct Row { int id = 0; QString name; qsizetype count = 0; };
    for (const QString& field : (dual
        ? QStringList{QStringLiteral("MATYPE"), QStringLiteral("LAYER")}
        : QStringList{primary}))
    {
        QMap<int, Row> groups;
        for (auto it = map.tagSets.constBegin(); it != map.tagSets.constEnd(); ++it)
        {
            const MeshTagSet& set = it.value();
            if (set.key.category.compare(field, Qt::CaseInsensitive) != 0 || set.key.dim != map.meshDim)
                continue;
            Row& row = groups[set.key.id];
            row.id = set.key.id;
            row.count += set.cellIds.size();
            const QString label = map.groupDisplayNames.value(
                QStringLiteral("%1/%2/%3").arg(field).arg(set.key.dim).arg(set.key.id));
            if (!label.trimmed().isEmpty()) row.name = label.trimmed();
            else if (!set.name.trimmed().isEmpty()) row.name = set.name.trimmed();
        }
        QTableWidget* table = field == QStringLiteral("LAYER")
            ? ui->tblLayerGroups : ui->tblMatypeGroups;
        table->setRowCount(groups.size());
        int rowNumber = 0;
        for (auto it = groups.begin(); it != groups.end(); ++it, ++rowNumber)
        {
            auto* id = new QTableWidgetItem(QString::number(it->id));
            auto* name = new QTableWidgetItem(it->name.isEmpty()
                ? QStringLiteral("%1=%2").arg(field).arg(it->id) : it->name);
            auto* count = new QTableWidgetItem(QString::number(it->count));
            id->setTextAlignment(Qt::AlignCenter);
            count->setTextAlignment(Qt::AlignCenter);
            table->setItem(rowNumber, 0, id);
            table->setItem(rowNumber, 1, name);
            table->setItem(rowNumber, 2, count);
        }
        table->resizeColumnsToContents();
        const QSet<int>& prior = field == QStringLiteral("LAYER") ? layPrior : matPrior;
        for (int row = 0; row < table->rowCount(); ++row)
            if (auto* item = table->item(row, 0); item && prior.contains(item->text().toInt()))
                table->selectionModel()->select(table->model()->index(row, 0),
                    QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
    m_rebuildingGroups = false;
    if (groupingChanged) syncGroupTab();
    else updateGroupActionState();
}

void MeshGroupingWidget::refreshBoundaryGroupList()
{
    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    ui->tblBoundaryGroups->clearContents();
    ui->tblBoundaryGroups->setRowCount(0);
    if (!meshService)
        return;

    QVector<MeshTagSet> boundarySets;
    if (m_hasAutoBoundaryGroups)
    {
        for (auto it = m_autoBoundaryPickMap.tagSets.constBegin();
             it != m_autoBoundaryPickMap.tagSets.constEnd(); ++it)
            if (it.key().dim >= 0 && it.key().dim <= 2
                && (it.key().category == QStringLiteral("Physical")
                    || it.key().category == QStringLiteral("SmeshBoundary")))
                boundarySets.push_back(it.value());
    }
    else
        boundarySets = meshService->boundaryTagSets();

    std::sort(boundarySets.begin(), boundarySets.end(),
              [](const MeshTagSet& a, const MeshTagSet& b)
              {
                  if (a.key.dim != b.key.dim)
                      return a.key.dim < b.key.dim;
                  if (a.key.category != b.key.category)
                      return a.key.category < b.key.category;
                  return a.key.id < b.key.id;
              });

    ui->tblBoundaryGroups->setRowCount(boundarySets.size());

    int row = 0;
    for (const MeshTagSet& set : boundarySets)
    {
        const int dim = set.key.dim;
        const QString dimName =
            (dim == 0) ? QStringLiteral("点边界")
                       : (dim == 1 ? QStringLiteral("线边界") : QStringLiteral("面边界"));

        const int count = set.pointIds.size();
        const QString name = set.name.trimmed().isEmpty()
                                 ? QStringLiteral("%1%2").arg(dimName).arg(set.key.id)
                                 : set.name.trimmed();

        ui->tblBoundaryGroups->setItem(row, 0,
                                       new QTableWidgetItem(QString::number(set.key.id)));
        ui->tblBoundaryGroups->setItem(row, 1, new QTableWidgetItem(name));
        ui->tblBoundaryGroups->setItem(row, 2, new QTableWidgetItem(dimName));
        ui->tblBoundaryGroups->setItem(row, 3, new QTableWidgetItem(QString::number(count)));
        ++row;
    }

    ui->tblBoundaryGroups->setRowCount(row);
    ui->tblBoundaryGroups->resizeColumnsToContents();
}

void MeshGroupingWidget::buildManualGroupingControls()
{
    // Group and local-cell editing share ONE target and ONE apply action.
    // Only the source of the selected cell IDs is different.
    m_groupActions = new QGroupBox(QStringLiteral("调整分组"), this);
    auto* layout = new QVBoxLayout(m_groupActions);

    auto* modeRow = new QHBoxLayout();
    m_modeLabel = new QLabel(QStringLiteral("选择范围"), m_groupActions);
    m_selectMode = new QComboBox(m_groupActions);
    m_selectMode->addItem(QStringLiteral("整组选择"));
    m_selectMode->addItem(QStringLiteral("局部单元选择"));
    modeRow->addWidget(m_modeLabel);
    modeRow->addWidget(m_selectMode, 1);
    layout->addLayout(modeRow);


    auto* selectedRow = new QHBoxLayout();
    m_manualStatus = new QLabel(QStringLiteral("当前选择：0 个单元"), m_groupActions);
    m_clearButton = new QPushButton(QStringLiteral("清空选择"), m_groupActions);
    selectedRow->addWidget(m_manualStatus, 1);
    selectedRow->addWidget(m_clearButton);
    layout->addLayout(selectedRow);

    auto* targetRow = new QHBoxLayout();
    m_targetLabel = new QLabel(QStringLiteral("目标编号"), m_groupActions);
    m_mergeTargetId = new QSpinBox(m_groupActions);
    m_mergeTargetId->setRange(3, MAX_SOLVER_MATYPE);
    m_mergeTargetId->setValue(3);
    m_mergeTargetId->setToolTip(QStringLiteral("目标编号可选择现有组或尚未使用的编号。"));
    m_mergeTargetName = new QLineEdit(m_groupActions);
    m_mergeTargetName->setPlaceholderText(QStringLiteral("目标组名（选填）"));
    targetRow->addWidget(m_targetLabel);
    targetRow->addWidget(m_mergeTargetId);
    targetRow->addWidget(m_mergeTargetName, 1);
    layout->addLayout(targetRow);

    m_mergeButton = new QPushButton(QStringLiteral("应用分组"), m_groupActions);
    layout->addWidget(m_mergeButton);
    ui->contentLayout->insertWidget(1, m_groupActions);

    // Selection sets remain separate data; they do NOT write MATYPE/LAYER.
    auto* setsPage = new QWidget(this);
    m_selectionSetsPage = setsPage;
    auto* setsLayout = new QVBoxLayout(setsPage);
    m_selectionSetsTable = new QTableWidget(setsPage);
    m_selectionSetsTable->setColumnCount(3);
    m_selectionSetsTable->setHorizontalHeaderLabels(
        {QStringLiteral("编号"), QStringLiteral("名称"), QStringLiteral("单元数")});
    m_selectionSetsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_selectionSetsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_selectionSetsTable->horizontalHeader()->setStretchLastSection(true);
    setsLayout->addWidget(m_selectionSetsTable);
    auto* buttons = new QHBoxLayout();
    auto* save = new QPushButton(QStringLiteral("新建选择集"), setsPage);
    auto* add = new QPushButton(QStringLiteral("加入所选"), setsPage);
    auto* remove = new QPushButton(QStringLiteral("移出所选"), setsPage);
    auto* del = new QPushButton(QStringLiteral("删除选择集"), setsPage);
    buttons->addWidget(save); buttons->addWidget(add);
    buttons->addWidget(remove); buttons->addWidget(del);
    setsLayout->addLayout(buttons);
    ui->tabGroupingInfo->addTab(setsPage, QStringLiteral("选择集"));

    for (QTableWidget* table : {ui->tblMatypeGroups, ui->tblLayerGroups})
    {
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::ExtendedSelection);
        connect(table, &QTableWidget::itemSelectionChanged, this,
                [this]() { if (!m_rebuildingGroups) selectGroupsFromTable(); });
    }

    connect(m_selectMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { syncGroupTab(); });
    connect(m_clearButton, &QPushButton::clicked, this, [this]() {
        if (!m_advancedCellMode)
        {
            if (QTableWidget* table = currentGroupTable())
                table->clearSelection();
        }
        clearManualSelection();
    });
    connect(m_mergeButton, &QPushButton::clicked, this, [this]() {
        if (m_advancedCellMode) applyManualGrouping();
        else mergeSelectedGroups();
    });
    connect(save, &QPushButton::clicked, this, [this]() { writeSelectionSet("create"); });
    connect(add, &QPushButton::clicked, this, [this]() { writeSelectionSet("add"); });
    connect(remove, &QPushButton::clicked, this, [this]() { writeSelectionSet("remove"); });
    connect(del, &QPushButton::clicked, this, &MeshGroupingWidget::deleteSelectionSet);
    connect(ui->tabGroupingInfo, &QTabWidget::currentChanged, this,
            [this](int) {
                if (m_mergeTargetName) m_mergeTargetName->clear();
                syncGroupTab();
            });
    updateGroupActionState();
}

void MeshGroupingWidget::syncGroupTab()
{
    // Selection-set creation needs individual members. For material and layer
    // tabs, the user explicitly chooses whole groups or local cells.
    const bool groupTab = ui->tabGroupingInfo->currentWidget() == ui->tabMaterial
        || ui->tabGroupingInfo->currentWidget() == ui->tabLayer;
    m_advancedCellMode = isSelectionTab() || (groupTab && m_selectMode->currentIndex() == 1);
    if (m_advancedCellMode && groupTab)
    {
        // In local mode the group-list highlight is not the editable cell selection.
        // Drop it so the user does not mistake a whole group for selected cells.
        if (QTableWidget* table = currentGroupTable())
        {
            QSignalBlocker block(table);
            table->clearSelection();
        }
    }
    clearManualSelection();
    if (!m_advancedCellMode && groupTab)
        selectGroupsFromTable();
    updateGroupActionState();
}

void MeshGroupingWidget::updateGroupActionState()
{
    const int tab = ui->tabGroupingInfo->currentIndex();
    const bool groupTab = ui->tabGroupingInfo->currentWidget() == ui->tabMaterial
        || ui->tabGroupingInfo->currentWidget() == ui->tabLayer;
    const bool selectionTab = isSelectionTab();
    if (m_groupActions) m_groupActions->setVisible(groupTab || selectionTab);
    if (m_modeLabel) m_modeLabel->setVisible(groupTab);
    if (m_selectMode) m_selectMode->setVisible(groupTab);
    if (m_modeHint)
    {
        m_modeHint->setText(selectionTab
            ? QStringLiteral("点选或框选网格单元后，可创建选择集或调整选择集成员。")
            : m_advancedCellMode
                ? QStringLiteral("点击网格选择单元；Shift 框选添加、Ctrl 框选移除。内部单元需先隔离或剖切。")
                : QStringLiteral("点击上方组名或视图中的区域选择整组；Ctrl/Shift 可多选组。"));
    }
    if (m_mergeTargetId)
    {
        const QString field = groupFieldForTab(tab);
        m_mergeTargetId->setRange(field == QStringLiteral("MATYPE") ? 3 : 1,
            field == QStringLiteral("MATYPE") ? MAX_SOLVER_MATYPE : 1000000);
        m_mergeTargetId->setVisible(groupTab);
    }
    if (m_targetLabel) m_targetLabel->setVisible(groupTab);
    if (m_mergeTargetName)
        m_mergeTargetName->setPlaceholderText(selectionTab
            ? QStringLiteral("选择集名称（选填）") : QStringLiteral("目标组名（选填）"));
    if (m_mergeButton)
    {
        m_mergeButton->setVisible(groupTab);
        QTableWidget* table = currentGroupTable();
        const bool hasGroups = table && table->selectionModel()
            && !table->selectionModel()->selectedRows().isEmpty();
        m_mergeButton->setEnabled(m_advancedCellMode
            ? !m_manualCellIds.isEmpty() : hasGroups);
    }
}

void MeshGroupingWidget::selectGroupsFromTable()
{
    updateGroupActionState();
    if (m_advancedCellMode) return;
    const int tab = ui->tabGroupingInfo->currentIndex();
    if (ui->tabGroupingInfo->currentWidget() != ui->tabMaterial
        && ui->tabGroupingInfo->currentWidget() != ui->tabLayer) return;
    QTableWidget* table = currentGroupTable();
    if (!table || !table->selectionModel()) return;
    auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!svc) return;
    const MshPickMap map = svc->mshPickMapSolid();
    const QString field = groupFieldForTab(tab);
    QSet<int> selected;
    for (const QModelIndex& index : table->selectionModel()->selectedRows())
        if (const auto* item = table->item(index.row(), 0))
            selected.insert(item->text().toInt());
    m_manualCellIds.clear();
    for (auto it = map.tagSets.constBegin(); it != map.tagSets.constEnd(); ++it)
    {
        const MeshTagSet& set = it.value();
        if (set.key.category == field && set.key.dim == map.meshDim
            && selected.contains(set.key.id))
            for (MeshIdType cid : set.cellIds) m_manualCellIds.insert(cid);
    }
    // Stop the old Face selection from affecting subsequent group clicks.
    m_ignoreFaceSignal = true;
    PickManager::instance().clearSelection(kPickDomain, kPickUuid, PickMode::Face);
    m_ignoreFaceSignal = false;
    m_lastFaceKeys.clear();
    updateManualSelectionPreview();
}

void MeshGroupingWidget::mergeSelectedGroups()
{
    const int tab = ui->tabGroupingInfo->currentIndex();
    if (ui->tabGroupingInfo->currentWidget() != ui->tabMaterial
        && ui->tabGroupingInfo->currentWidget() != ui->tabLayer) return;
    QTableWidget* table = currentGroupTable();
    if (!table || !table->selectionModel()) return;
    auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!svc) return;
    const QString field = groupFieldForTab(tab);
    const int target = m_mergeTargetId->value();
    QSet<int> sources;
    for (const QModelIndex& index : table->selectionModel()->selectedRows())
        if (const auto* item = table->item(index.row(), 0))
            sources.insert(item->text().toInt());
    sources.remove(target); // Selecting the target is harmless.
    for (int source : sources)
    {
        if ((field == QStringLiteral("MATYPE") && source < 3) ||
            (field == QStringLiteral("LAYER") && source <= 0) ||
            (field != QStringLiteral("MATYPE") && field != QStringLiteral("LAYER") && source <= 0))
        {
            QMessageBox::warning(this, QStringLiteral("合并分组"),
                QStringLiteral("所选组包含求解器保留材料编号或杆单元安装层号，不能直接合并。"));
            return;
        }
    }
    if (sources.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("合并分组"),
            QStringLiteral("请在列表中选择一个或多个需要合并的来源组，并填写不同的目标组编号。"));
        return;
    }
    const MshPickMap map = svc->mshPickMapSolid();
    QVector<MeshIdType> ids;
    for (auto it = map.tagSets.constBegin(); it != map.tagSets.constEnd(); ++it)
    {
        const MeshTagSet& set = it.value();
        if (set.key.category.compare(field, Qt::CaseInsensitive) != 0
            || !sources.contains(set.key.id)) continue;
        if (set.key.dim != map.meshDim)
        {
            // Physical boundary tags may share an ID with volume groups.
            // They are independent and must never be moved as volume cells.
            if ((field == QStringLiteral("MATYPE") || field == QStringLiteral("LAYER"))
                && !set.cellIds.isEmpty())
            {
                QMessageBox::warning(this, QStringLiteral("合并分组"),
                    QStringLiteral("所选组包含其他维度单元，请在专用页面处理。"));
                return;
            }
            continue;
        }
        ids += set.cellIds;
    }
    if (ids.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("合并分组"),
            QStringLiteral("所选分组中没有可修改的二维/三维实体单元。"));
        return;
    }
    QStringList sourceNames;
    for (int id : sources) sourceNames.append(QString::number(id));
    sourceNames.sort();
    const QString warning = field == QStringLiteral("LAYER")
        ? QStringLiteral("施工层合并可能改变开挖/填筑阶段的作用范围，原工况中引用的旧层号不会自动替换。")
        : field == QStringLiteral("MATYPE")
            ? QStringLiteral("材料组合并会改变单元材料编号；请确认目标组已配置正确的材料参数，旧材料关联不会自动迁移。")
            : QStringLiteral("修改原始区域会影响已有业务区域引用，MATYPE/LAYER 保持不变。请检查已设置的开挖或坝体区域。");
    const QString details = QStringLiteral("将 %1 组 [%2] 的 %3 个单元合并到 %4=%5？\n\n%6\n\n另一套 MATYPE/LAYER 属性保持不变。")
        .arg(field, sourceNames.join(QStringLiteral(", "))).arg(ids.size())
        .arg(field).arg(target).arg(warning);
    if (QMessageBox::question(this, QStringLiteral("确认合并分组"), details,
         QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    QString error;
    if (!svc->assignSelectedCellsToGroup(ids, field, target,
                                          m_mergeTargetName->text().trimmed(), &error))
    {
        QMessageBox::warning(this, QStringLiteral("合并失败"), error);
        return;
    }
    // Group names are metadata. Once a source id has no remaining members,
    // remove its old name, otherwise re-using the id later displays stale text.
    MshPickMap resultMap = svc->mshPickMapSolid();
    bool namesChanged = false;
    for (int source : sources)
    {
        bool stillExists = false;
        for (auto it = resultMap.tagSets.constBegin(); it != resultMap.tagSets.constEnd(); ++it)
            if (it.key().category == field && it.key().id == source && !it.value().cellIds.isEmpty())
            {
                stillExists = true;
                break;
            }
        if (!stillExists)
        {
            const QString nameKey = QStringLiteral("%1/%2/%3").arg(field).arg(map.meshDim).arg(source);
            namesChanged = resultMap.groupDisplayNames.remove(nameKey) > 0 || namesChanged;
        }
    }
    if (namesChanged) svc->setMshPickMapSolid(resultMap);
    clearManualSelection();
    refreshGroupList();
    table->clearSelection();
    for (int row = 0; row < table->rowCount(); ++row)
    {
        const auto* item = table->item(row, 0);
        if (item && item->text().toInt() == target)
        {
            table->selectRow(row);
            break;
        }
    }
}

QTableWidget* MeshGroupingWidget::currentGroupTable() const
{
    if (ui->tabGroupingInfo->currentWidget() == ui->tabMaterial) return ui->tblMatypeGroups;
    if (ui->tabGroupingInfo->currentWidget() == ui->tabLayer) return ui->tblLayerGroups;
    if (isSelectionTab()) return m_selectionSetsTable;
    return nullptr;
}

int MeshGroupingWidget::selectedGroupId() const
{
    QTableWidget* table = currentGroupTable();
    if (!table || table->currentRow() < 0)
        return 0;
    const auto* item = table->item(table->currentRow(), 0);
    return item ? item->text().toInt() : 0;
}

void MeshGroupingWidget::updateManualCount()
{
    if (m_manualStatus)
        m_manualStatus->setText(QStringLiteral("当前选择：%1 个单元%2")
            .arg(m_manualCellIds.size())
            .arg(m_manualCellIds.size() > kMaxPreviewCells
                     ? QStringLiteral("（仅高亮预览前 %1 个）").arg(kMaxPreviewCells)
                     : QString()));
    updateGroupActionState();
}

void MeshGroupingWidget::onPickedFacesChanged()
{
    auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    vtkUnstructuredGrid* full = svc ? svc->getVtkUnstructuredGrid() : nullptr;
    if (!full) return;
    const auto keys = PickManager::instance().getIdList(kPickDomain, kPickUuid, PickMode::Face);
    int meshDim = 0;
    for (vtkIdType cid = 0; cid < full->GetNumberOfCells(); ++cid)
        if (vtkCell* cell = full->GetCell(cid)) meshDim = qMax(meshDim, cell->GetCellDimension());
    QSet<qint64> now;
    for (vtkIdType k : keys) now.insert(static_cast<qint64>(k));

    const int tab = ui->tabGroupingInfo->currentIndex();
    if (ui->tabGroupingInfo->currentWidget() == ui->tabBoundary) return;
    if (!m_advancedCellMode &&
        (ui->tabGroupingInfo->currentWidget() == ui->tabMaterial
         || ui->tabGroupingInfo->currentWidget() == ui->tabLayer))
    {
        const QString field = groupFieldForTab(tab);
        const MshPickMap map = svc->mshPickMapSolid();
        QTableWidget* table = ui->tabGroupingInfo->currentWidget() == ui->tabMaterial
            ? ui->tblMatypeGroups : ui->tblLayerGroups;
        // Only act on NEW picks; clearing old picks must not randomly change the group.
        for (vtkIdType key : keys)
        {
            if (m_lastFaceKeys.contains(static_cast<qint64>(key))) continue;
            const vtkIdType cid = key >> 4;
            if (cid < 0 || cid >= full->GetNumberOfCells()) continue;
            vtkCell* cell = full->GetCell(cid);
            if (!cell || cell->GetCellDimension() != meshDim || meshDim < 2) continue;
            int groupId = 0;
            for (const MeshTagKey& key : map.cellIdToDomainTags.value(cid))
                if (key.dim == meshDim && key.category.compare(field, Qt::CaseInsensitive) == 0)
                { groupId = key.id; break; }
            if (groupId == 0) continue;
            for (int row = 0; row < table->rowCount(); ++row)
            {
                const auto* item = table->item(row, 0);
                if (item && item->text().toInt() == groupId)
                {
                    // A click selects the complete material/layer group, not one face.
                    table->selectRow(row);
                    // Even when the same row was already selected, drop the raw face pick:
                    // the next click must be treated as a fresh group selection.
                    m_ignoreFaceSignal = true;
                    PickManager::instance().clearSelection(kPickDomain, kPickUuid, PickMode::Face);
                    m_ignoreFaceSignal = false;
                    m_lastFaceKeys.clear();
                    return;
                }
            }
        }
        m_lastFaceKeys = now;
        return;
    }
    // Face IDs are packed as (full-grid cell id << 4) | face local id.
    // Compare with the PREVIOUS face selection instead of rebuilding the
    // selected cell set, so a group selection can be edited with more clicks.
    const QSet<qint64> additions = now - m_lastFaceKeys;
    const QSet<qint64> removals = m_lastFaceKeys - now;
    for (qint64 key : additions)
    {
        const vtkIdType cid = static_cast<vtkIdType>(key >> 4);
        vtkCell* c = cid >= 0 && cid < full->GetNumberOfCells() ? full->GetCell(cid) : nullptr;
        if (c && c->GetCellDimension() == meshDim && meshDim >= 2)
            m_manualCellIds.insert(cid);
    }
    for (qint64 key : removals)
    {
        const vtkIdType cid = static_cast<vtkIdType>(key >> 4);
        m_manualCellIds.remove(cid);
    }
    m_lastFaceKeys = now;
    updateManualSelectionPreview();
}

void MeshGroupingWidget::clearManualSelection()
{
    m_manualCellIds.clear();
    m_lastFaceKeys.clear();
    m_ignoreFaceSignal = true;
    PickManager::instance().clearSelection(kPickDomain, kPickUuid, PickMode::Face);
    m_ignoreFaceSignal = false;
    updateManualSelectionPreview();
}

void MeshGroupingWidget::applyManualGrouping()
{
    const int tab = ui->tabGroupingInfo->currentIndex();
    if ((ui->tabGroupingInfo->currentWidget() != ui->tabMaterial
        && ui->tabGroupingInfo->currentWidget() != ui->tabLayer) || !m_advancedCellMode)
        return;
    auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!svc || m_manualCellIds.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("网格分组"), QStringLiteral("请先选择网格单元。"));
        return;
    }
    QVector<MeshIdType> ids;
    ids.reserve(m_manualCellIds.size());
    for (MeshIdType id : m_manualCellIds) ids.push_back(id);
    const QString field = groupFieldForTab(tab);
    const int target = m_mergeTargetId->value();
    const QString consequence = field == QStringLiteral("MATYPE")
        ? QStringLiteral("这会修改所选单元的材料编号，请核对相应材料参数关联。")
        : field == QStringLiteral("LAYER")
            ? QStringLiteral("这会修改所选单元的施工层号，请核对已有开挖/填筑工况。")
            : QStringLiteral("这会修改原始网格区域分组，不会更改 MATYPE/LAYER；请核对已配置的区域引用。");
    if (QMessageBox::question(this, QStringLiteral("确认调整分组"),
          QStringLiteral("将 %1 个单元设为 %2=%3？\n\n%4\n另一套 MATYPE/LAYER 属性保持不变。")
              .arg(ids.size()).arg(field).arg(target).arg(consequence),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    QString error;
    if (!svc->assignSelectedCellsToGroup(ids, field, target,
                                          m_mergeTargetName->text().trimmed(), &error))
    {
        QMessageBox::warning(this, QStringLiteral("重新分组失败"), error);
        return;
    }
    clearManualSelection();
    refreshGroupList();
}

void MeshGroupingWidget::refreshSelectionSets()
{
    if (!m_selectionSetsTable) return;
    m_selectionSetsTable->setRowCount(0);
    auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!svc) return;
    const MshPickMap map = svc->mshPickMapSolid();
    QList<MeshTagSet> sets;
    for (auto it = map.tagSets.constBegin(); it != map.tagSets.constEnd(); ++it)
        if (it.key().category == QStringLiteral("Selection") && it.key().dim >= 2)
            sets.push_back(it.value());
    std::sort(sets.begin(), sets.end(), [](const MeshTagSet& a, const MeshTagSet& b) {
        return a.key.id < b.key.id;
    });
    m_selectionSetsTable->setRowCount(sets.size());
    for (int i=0; i<sets.size(); ++i)
    {
        m_selectionSetsTable->setItem(i,0,new QTableWidgetItem(QString::number(sets[i].key.id)));
        m_selectionSetsTable->setItem(i,1,new QTableWidgetItem(sets[i].name));
        m_selectionSetsTable->setItem(i,2,new QTableWidgetItem(QString::number(sets[i].cellIds.size())));
    }
}

void MeshGroupingWidget::writeSelectionSet(const QString& action)
{
    auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    vtkUnstructuredGrid* grid = svc ? svc->getVtkUnstructuredGrid() : nullptr;
    if (!svc || !grid || m_manualCellIds.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("选择集"), QStringLiteral("请先选择网格单元。"));
        return;
    }
    MshPickMap map = svc->mshPickMapSolid();
    int dim = qMax(2, map.meshDim);
    int id = selectedGroupId();
    if (action == "create")
    {
        id = 1;
        while (map.tagSets.contains({QStringLiteral("Selection"), dim, id})) ++id;
    }
    if (id <= 0) return;
    MeshTagKey key{QStringLiteral("Selection"), dim, id};
    if (action != "create" && !map.tagSets.contains(key))
    {
        QMessageBox::warning(this, QStringLiteral("选择集"), QStringLiteral("请先选择已有选择集。"));
        return;
    }
    MeshTagSet set = map.tagSets.value(key);
    if (action == "create")
    {
        set.key = key;
        set.source = QStringLiteral("Manual");
        set.name = m_mergeTargetName ? m_mergeTargetName->text().trimmed() : QString();
        if (set.name.isEmpty()) set.name = QStringLiteral("选择集%1").arg(id);
    }
    QSet<MeshIdType> members;
    for (MeshIdType cid : set.cellIds) members.insert(cid);
    for (MeshIdType cid : m_manualCellIds)
    {
        if (cid < 0 || cid >= grid->GetNumberOfCells()) continue;
        vtkCell* cell = grid->GetCell(static_cast<vtkIdType>(cid));
        if (!cell || cell->GetCellDimension() != dim) continue;
        if (action == "remove") members.remove(cid);
        else members.insert(cid);
    }
    set.cellIds.clear();
    set.cellIds.reserve(members.size());
    for (MeshIdType cid : members) set.cellIds.push_back(cid);
    std::sort(set.cellIds.begin(), set.cellIds.end());
    map.tagSets.insert(key, set);
    svc->setMshPickMapSolid(map);
    refreshSelectionSets();
}

void MeshGroupingWidget::deleteSelectionSet()
{
    if (!isSelectionTab() || selectedGroupId() <= 0) return;
    auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!svc) return;
    MshPickMap map = svc->mshPickMapSolid();
    map.tagSets.remove({QStringLiteral("Selection"), qMax(2,map.meshDim), selectedGroupId()});
    svc->setMshPickMapSolid(map);
    refreshSelectionSets();
}

void MeshGroupingWidget::updateManualSelectionPreview()
{
    updateManualCount();
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual) return;
    visual->removeProp(kManualPreviewPropId, false);
    m_manualPreviewActor = nullptr;
    auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    vtkUnstructuredGrid* grid = svc ? svc->getVtkUnstructuredGrid() : nullptr;
    if (!grid || m_manualCellIds.isEmpty()) { visual->render(); return; }
    vtkNew<vtkIdList> ids;
    int shown = 0;
    for (MeshIdType cid : m_manualCellIds)
    {
        if (cid >= 0 && cid < grid->GetNumberOfCells()) ids->InsertNextId(cid);
        if (++shown >= kMaxPreviewCells) break;
    }
    if (ids->GetNumberOfIds() == 0) { visual->render(); return; }
    vtkNew<vtkExtractCells> extract;
    extract->SetInputData(grid);
    extract->SetCellList(ids);
    vtkNew<vtkDataSetSurfaceFilter> surface;
    surface->SetInputConnection(extract->GetOutputPort());
    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputConnection(surface->GetOutputPort());
    mapper->ScalarVisibilityOff();
    // This actor overlaps the original mesh surface. Draw it slightly closer
    // instead of producing z-fighting and a second dense orange wireframe.
    vtkMapper::SetResolveCoincidentTopologyToPolygonOffset();
    mapper->SetRelativeCoincidentTopologyPolygonOffsetParameters(-2.0, -2.0);
    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->PickableOff(); // Preview should not intercept further mesh picking.
    actor->GetProperty()->SetColor(1.0,0.56,0.13);
    actor->GetProperty()->SetOpacity(0.26);
    actor->GetProperty()->EdgeVisibilityOff();
    actor->GetProperty()->SetLighting(false);
    visual->addProp(kManualPreviewPropId, actor, "网格", "选择预览", false, false);
    m_manualPreviewActor = actor;
    visual->render();
}

void MeshGroupingWidget::updateRangePreview(bool forceOn)
{
    if (!forceOn && !ui->chkShowRangeBox->isChecked())
    {
        clearRangePreview(false);
        return;
    }

    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual)
        return;

    double xmin = ui->dsXMin->value(), xmax = ui->dsXMax->value();
    double ymin = ui->dsYMin->value(), ymax = ui->dsYMax->value();
    double zmin = ui->dsZMin->value(), zmax = ui->dsZMax->value();

    if (xmin > xmax)
        std::swap(xmin, xmax);
    if (ymin > ymax)
        std::swap(ymin, ymax);
    if (zmin > zmax)
        std::swap(zmin, zmax);

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

    if (std::abs(xmax - xmin) < 1e-12 || std::abs(ymax - ymin) < 1e-12 ||
        std::abs(zmax - zmin) < 1e-12)
    {
        auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
        vtkSmartPointer<vtkUnstructuredGrid> grid =
            meshService ? meshService->getVtkUnstructuredGridSolid() : nullptr;
        if ((!grid || grid->GetNumberOfPoints() <= 0) && meshService)
        {
            grid = meshService->getVtkUnstructuredGrid();
        }
        if (grid && grid->GetNumberOfPoints() > 0)
        {
            double gb[6] = {0, 0, 0, 0, 0, 0};
            grid->GetBounds(gb);
            const double eps = std::max({std::abs(gb[1] - gb[0]), std::abs(gb[3] - gb[2]),
                                         std::abs(gb[5] - gb[4]), 1.0}) *
                               1e-4;
            if (std::abs(xmax - xmin) < 1e-12)
            {
                xmin -= eps;
                xmax += eps;
            }
            if (std::abs(ymax - ymin) < 1e-12)
            {
                ymin -= eps;
                ymax += eps;
            }
            if (std::abs(zmax - zmin) < 1e-12)
            {
                zmin -= eps;
                zmax += eps;
            }
            cube->SetBounds(xmin, xmax, ymin, ymax, zmin, zmax);
        }
    }

    visual->removeProp(kRangePreviewPropId, false);
    visual->addProp(kRangePreviewPropId, actor, "网格", "分组范围框", false, true);
    m_rangePreviewActor = actor;
}

void MeshGroupingWidget::clearRangePreview(bool renderNow)
{
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual)
        return;
    visual->removeProp(kRangePreviewPropId, renderNow);
    m_rangePreviewActor = nullptr;
}
