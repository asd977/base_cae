#include "MeshGroupingWidget.h"

#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/VisualWidgetService.h>
#include <struct/MeshMap.h>
#include <vtkActor.h>
#include <vtkCell.h>
#include <vtkCellData.h>
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
#include <vtkPolyDataNormals.h>
#include <vtkProperty.h>
#include <vtkStringArray.h>
#include <vtkUnstructuredGrid.h>

#include <QMap>
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
constexpr int FIRST_COMPUTABLE_MATYPE = 2;

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
    ui->cmbTargetArray->addItem("材料分组(MATYPE)", "MATYPE");
    ui->cmbTargetArray->addItem("填筑层分组(LAYER)", "LAYER");
    ui->cmbRangeTargetArray->clear();
    ui->cmbRangeTargetArray->addItem("材料分组(MATYPE)", "MATYPE");
    ui->cmbRangeTargetArray->addItem("填筑层分组(LAYER)", "LAYER");

    auto syncMatypeNumberBounds = [this]()
    {
        const bool autoTargetIsMatype =
            ui->cmbTargetArray->currentData().toString().compare("MATYPE", Qt::CaseInsensitive) == 0;
        ui->spStartValue->setMinimum(autoTargetIsMatype ? FIRST_COMPUTABLE_MATYPE : 1);
        if (autoTargetIsMatype && ui->spStartValue->value() < FIRST_COMPUTABLE_MATYPE)
            ui->spStartValue->setValue(FIRST_COMPUTABLE_MATYPE);

        const bool rangeTargetIsMatype =
            ui->cmbRangeTargetArray->currentData().toString().compare("MATYPE", Qt::CaseInsensitive) == 0;
        ui->spTargetValue->setMinimum(rangeTargetIsMatype ? FIRST_COMPUTABLE_MATYPE : 1);
        if (rangeTargetIsMatype && ui->spTargetValue->value() < FIRST_COMPUTABLE_MATYPE)
            ui->spTargetValue->setValue(FIRST_COMPUTABLE_MATYPE);
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
    if (ui->chkShowRangeBox->isChecked())
    {
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
    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshService)
    {
        QMessageBox::warning(this, "网格分组", "MeshModuleControllerService 不可用");
        return;
    }

    const QString targetArray = ui->cmbTargetArray->currentData().toString().trimmed();
    QString prefix = targetArray == "LAYER" ? "LayerGroup" : "MaterialGroup";

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
        QMessageBox::warning(this, "网格分组", "MeshModuleControllerService 不可用");
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
        QMessageBox::warning(this, "边界自动分组", "MeshModuleControllerService 不可用");
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
    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshService)
    {
        ui->tblMatypeGroups->setRowCount(0);
        ui->tblLayerGroups->setRowCount(0);
        return;
    }

    const MshPickMap pick = meshService->mshPickMapSolid();

    auto fillTableByMap = [&](QTableWidget* table, const QHash<int, QVector<qint64>>& valueToCells,
                              const QHash<qint64, QString>& cellToGroupName)
    {
        table->clearContents();
        table->setRowCount(0);

        QMap<QString, int> counter;
        for (auto it = valueToCells.begin(); it != valueToCells.end(); ++it)
        {
            const int value = it.key();
            for (qint64 cid : it.value())
            {
                const QString g = cellToGroupName.value(cid).trimmed();
                const QString key = QString("%1|%2").arg(value).arg(g.isEmpty() ? "(未命名)" : g);
                counter[key] += 1;
            }
        }

        table->setRowCount(counter.size());
        int row = 0;
        for (auto it = counter.begin(); it != counter.end(); ++it, ++row)
        {
            const QStringList parts = it.key().split('|');
            auto* itemVal = new QTableWidgetItem(parts.value(0));
            auto* itemName = new QTableWidgetItem(parts.value(1));
            auto* itemCount = new QTableWidgetItem(QString::number(it.value()));
            itemVal->setTextAlignment(Qt::AlignCenter);
            itemCount->setTextAlignment(Qt::AlignCenter);
            table->setItem(row, 0, itemVal);
            table->setItem(row, 1, itemName);
            table->setItem(row, 2, itemCount);
        }
        table->resizeColumnsToContents();
    };

    QHash<int, QVector<qint64>> matypeToCells;
    QHash<int, QVector<qint64>> layerToCells;
    QHash<qint64, QString> matypeNameByCell;
    QHash<qint64, QString> layerNameByCell;
    for (auto it = pick.tagSets.constBegin(); it != pick.tagSets.constEnd(); ++it)
    {
        const MeshTagSet& set = it.value();
        if (set.key.category.compare("MATYPE", Qt::CaseInsensitive) == 0)
        {
            auto& cells = matypeToCells[set.key.id];
            for (MeshIdType cid : set.cellIds)
            {
                cells.push_back(static_cast<qint64>(cid));
                matypeNameByCell[static_cast<qint64>(cid)] = set.name;
            }
        }
        else if (set.key.category.compare("LAYER", Qt::CaseInsensitive) == 0)
        {
            auto& cells = layerToCells[set.key.id];
            for (MeshIdType cid : set.cellIds)
            {
                cells.push_back(static_cast<qint64>(cid));
                layerNameByCell[static_cast<qint64>(cid)] = set.name;
            }
        }
    }

    fillTableByMap(ui->tblMatypeGroups, matypeToCells, matypeNameByCell);
    fillTableByMap(ui->tblLayerGroups, layerToCells, layerNameByCell);
}

void MeshGroupingWidget::refreshBoundaryGroupList()
{
    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    ui->tblBoundaryGroups->clearContents();
    ui->tblBoundaryGroups->setRowCount(0);
    if (!meshService)
        return;

    const MshPickMap pick =
        m_hasAutoBoundaryGroups ? m_autoBoundaryPickMap : meshService->mshPickMapSolid();

    QVector<MeshTagSet> boundarySets;
    for (auto it = pick.tagSets.constBegin(); it != pick.tagSets.constEnd(); ++it)
    {
        const MeshTagSet& set = it.value();
        if (set.key.category != QStringLiteral("Physical"))
            continue;
        if (set.key.dim < 0 || set.key.dim > 2)
            continue;
        boundarySets.push_back(set);
    }

    std::sort(boundarySets.begin(), boundarySets.end(),
              [](const MeshTagSet& a, const MeshTagSet& b)
              {
                  if (a.key.dim != b.key.dim)
                      return a.key.dim < b.key.dim;
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
