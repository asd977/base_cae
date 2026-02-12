#include "MeshModuleController.h"

#include <ProjectNode.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <services/VisualWidgetService.h>
#include <EventManager.h>
#include <GridEditingWidget.h>
#include <MeshGlobalMeshingWidget.h>
#include <MeshGroupingWidget.h>
#include <PublicConstants.h>
#include <QMessageBox>
#include <qfiledialog.h>
#include <QVector>
#include <vtkActor.h>
#include <vtkCell.h>
#include <vtkCellType.h>
#include <vtkDataSetMapper.h>
#include <vtkIntArray.h>
#include <vtkLookupTable.h>
#include <vtkPoints.h>
#include <vtkProperty.h>
#include <vtkUnstructuredGrid.h>
#include <vtkCellData.h>
#include <vtkPointData.h>
#include <vtkExtractCells.h>
#include <algorithm>
#include <limits>
#include <vtkIdList.h>
#include <vtkStringArray.h>

// === 这些头文件你后面真正实现时再加 ===
// #include "MeshImportGridWidget.h"
// #include "MeshExportGridWidget.h"
// #include "MeshGlobalMeshingWidget.h"
// #include "MeshSingleMeshingWidget.h"
// #include "MeshMappedMeshingWidget.h"
// #include "MeshSweepMeshingWidget.h"
// #include "MeshVolumeControlWidget.h"
// #include "MeshFaceControlWidget.h"
// #include "MeshEdgeControlWidget.h"
// #include "MeshQualityStandardWidget.h"

MeshModuleController::MeshModuleController(QObject* parent)
    : QObject(parent)
{
    ServiceRegistry::instance().registerService<MeshModuleControllerService>(this);
    m_visualWidgetService = ServiceRegistry::instance().getService<VisualWidgetService>();
    initAction();
}

void MeshModuleController::newProject()
{
    auto* projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>();
    m_gridNode = new ProjectNode("网格");
    projectManagerService->addTreeItem(m_gridNode);
}

bool MeshModuleController::saveProject()
{
    // TODO: 保存网格模块相关参数（剖分参数、控制参数、质量阈值、导入文件路径等）
    return true;
}

bool MeshModuleController::loadProject()
{
    // TODO: 读取并恢复网格模块相关参数
    return true;
}

void MeshModuleController::closeProject()
{
    // TODO: 释放/清理网格模块相关资源
}

QSet<int> MeshModuleController::getAvailableMatypes() const
{
    QSet<int> ret;
    if (!m_importedGrid) return ret;

    auto* arr = vtkIntArray::SafeDownCast(m_importedGrid->GetCellData()->GetArray("MATYPE"));
    if (!arr) return ret;

    const vtkIdType n = arr->GetNumberOfTuples();
    for (vtkIdType i = 0; i < n; ++i) ret.insert(arr->GetValue(i));
    return ret;
}


void MeshModuleController::highlightMatypes(const QSet<int> &matypes,
                                            int selectedMatype,
                                            int previewMatype,
                                            int preDeleteMatype)
{
    if (!m_gridEditingWidget) {
        if (!m_visualWidgetService) {
            return;
        }

        QList<vtkDataSetMapper*> targetMappers;
        if (m_previewSurfaceMapper) targetMappers.push_back(m_previewSurfaceMapper);
        if (m_previewLineMapper) targetMappers.push_back(m_previewLineMapper);
        if (targetMappers.isEmpty() && m_previewMapper) targetMappers.push_back(m_previewMapper);
        if (targetMappers.isEmpty()) {
            return;
        }

        const bool hasSelection = !matypes.isEmpty()
            || selectedMatype >= 0
            || previewMatype >= 0
            || preDeleteMatype >= 0;

        if (!hasSelection) {
            for (auto *mapper : std::as_const(targetMappers)) {
                if (mapper) {
                    mapper->ScalarVisibilityOff();
                }
            }
            m_visualWidgetService->render();
            return;
        }

        QSet<int> allMatypes = getAvailableMatypes();
        if (selectedMatype >= 0) {
            allMatypes.insert(selectedMatype);
        }
        if (previewMatype >= 0) {
            allMatypes.insert(previewMatype);
        }
        if (preDeleteMatype >= 0) {
            allMatypes.insert(preDeleteMatype);
        }
        allMatypes.unite(matypes);

        int maxMatype = -1;
        for (int v : std::as_const(allMatypes)) {
            maxMatype = qMax(maxMatype, v);
        }
        if (maxMatype < 0) {
            return;
        }

        auto lut = vtkSmartPointer<vtkLookupTable>::New();
        lut->SetNumberOfTableValues(maxMatype + 1);
        lut->SetTableRange(0.0, maxMatype);
        lut->Build();

        for (int i = 0; i <= maxMatype; ++i) {
            lut->SetTableValue(i, 0.75, 0.75, 0.75, 1.0);
        }
        for (int matype : std::as_const(matypes)) {
            if (matype >= 0 && matype <= maxMatype) {
                lut->SetTableValue(matype, 0.25, 0.75, 0.25, 1.0);
            }
        }
        if (selectedMatype >= 0 && selectedMatype <= maxMatype) {
            lut->SetTableValue(selectedMatype, 0.9, 0.2, 0.2, 1.0);
        }
        if (previewMatype >= 0 && previewMatype <= maxMatype) {
            lut->SetTableValue(previewMatype, 0.2, 0.4, 0.9, 1.0);
        }
        if (preDeleteMatype >= 0 && preDeleteMatype <= maxMatype) {
            lut->SetTableValue(preDeleteMatype, 0.9, 0.55, 0.2, 1.0);
        }

        for (auto *mapper : std::as_const(targetMappers)) {
            if (!mapper) {
                continue;
            }
            mapper->SetScalarModeToUseCellFieldData();
            mapper->SelectColorArray("MATYPE");
            mapper->SetLookupTable(lut);
            mapper->UseLookupTableScalarRangeOn();
            mapper->ScalarVisibilityOn();
        }
        m_visualWidgetService->render();
        return;
    }
    m_gridEditingWidget->highlightMatypes(matypes, selectedMatype, previewMatype, preDeleteMatype);
}

void MeshModuleController::initAction()
{
    auto &em = EventManager::instance();

    auto reg = [&](const char* actionId, std::function<void()> fn)
    {
        em.registerHandler(actionId, [this, fn](const QVariantMap &)
                           {
                               fn();
                           });
    };

    // --- IO ---
    reg(PublicConstants::kActionPreprocessImportGrid, [this]{ onImportGrid(); });
    reg(PublicConstants::kActionPreprocessExportGrid, [this]{ onExportGrid(); });

    // --- Meshing ---
    reg(PublicConstants::kActionPreprocessMeshGlobal, [this]{ onMeshGlobal(); });
    reg(PublicConstants::kActionPreprocessMeshSingle, [this]{ onMeshSingle(); });
    reg(PublicConstants::kActionPreprocessMeshMapped, [this]{ onMeshMapped(); });
    reg(PublicConstants::kActionPreprocessMeshSweep,  [this]{ onMeshSweep(); });

    reg(PublicConstants::kActionPreprocessMeshEdit,  [this]{ onMeshEdit(); });
    reg(PublicConstants::kActionPreprocessMeshGrouping, [this]{ onMeshGrouping(); });


    // --- Control & Quality ---
    reg(PublicConstants::kActionPreprocessCtrlVolume,  [this]{ onCtrlVolume(); });
    reg(PublicConstants::kActionPreprocessCtrlSurface, [this]{ onCtrlSurface(); });
    reg(PublicConstants::kActionPreprocessCtrlEdge,    [this]{ onCtrlEdge(); });      // NEW
    reg(PublicConstants::kActionPreprocessQualityStd,  [this]{ onQualityStd(); });
}

// -------------------- handlers --------------------
void MeshModuleController::onImportGrid()
{
    QString fileName;
    QFileDialog dialog(nullptr, QStringLiteral("选择网格文件"));
    dialog.setNameFilter(QStringLiteral("Grid Files (*.nas *.inp *.flac3d *.f3grid *.DAT *.msh)"));
    dialog.setFilter(QDir::Files);
    if (dialog.exec() == QDialog::Accepted) {
        fileName = dialog.selectedFiles().first();
    }
    if (fileName.isEmpty()) return;

    QFileInfo fi(fileName);
    const QString ext = fi.suffix().toLower();

    QString err;
    vtkSmartPointer<vtkUnstructuredGrid> grid;

    if (ext == "nas") {
        QMessageBox::warning(nullptr, QStringLiteral("不支持的格式"), QStringLiteral("当前暂不支持 .nas 格式"));
        return;
    } else if (ext == "flac3d" || ext == "f3grid") {
        grid = MeshGridIO::instance().importFlac3D(fileName, &err);
    } else if (ext == "inp") {
        grid = MeshGridIO::instance().importInp(fileName, &err);
    } else if (ext == "dat") {
        grid = MeshGridIO::instance().loadIiinFile(fileName, &err);
    } else if (ext == "msh") {
        grid = MeshGridIO::instance().loadMshFile(fileName, &err);
    }
    else {
        QMessageBox::warning(nullptr, QStringLiteral("不支持的格式"),
                             QStringLiteral("当前只支持 .nas / .inp / .flac3d / .f3grid / .dat"));
        return;
    }

    if (!grid || grid->GetNumberOfCells() == 0) {
        QMessageBox::warning(nullptr, QStringLiteral("导入失败"),
                             err.isEmpty() ? QStringLiteral("网格导入失败") : err);
        return;
    }

    m_importedGrid = grid;
    m_hasGrid = true;
    m_sourceFileName = fileName;

    displayCurrentGrid();
}

void MeshModuleController::importMshGrid(QString path)
{
    vtkSmartPointer<vtkUnstructuredGrid> grid;
    QString err;
    grid = MeshGridIO::instance().loadMshFile(path, &err);

    m_importedGrid = grid;
    m_hasGrid = true;
    m_sourceFileName = path;

    m_visualWidgetService->clearAll();
    displayCurrentGrid();
}

MshPickMap MeshModuleController::mshPickMapSolid()
{
    return MeshGridIO::instance().getMeshMap();
}

void MeshModuleController::onExportGrid()
{
    if (!m_hasGrid || !m_importedGrid) {
        QMessageBox::warning(nullptr, QStringLiteral("无可导出的网格"), QStringLiteral("请先导入网格后再导出。"));
        return;
    }

    QString defaultPath = m_sourceFileName.isEmpty()
        ? QStringLiteral("IIIN.dat")
        : MeshGridIO::defaultIiinOutputPath(m_sourceFileName);

    QString fileName = QFileDialog::getSaveFileName(nullptr,
                                                    QStringLiteral("导出 IIIN.DAT"),
                                                    defaultPath,
                                                    QStringLiteral("IIIN Grid (*.dat)"));
    if (fileName.isEmpty()) return;

    QString err;
    if (!MeshGridIO::instance().exportIiinFile(fileName, m_importedGrid, &err)) {
        QMessageBox::warning(nullptr, QStringLiteral("导出失败"),
                             err.isEmpty() ? QStringLiteral("IIIN.DAT 导出失败") : err);
        return;
    }
}

void MeshModuleController::onMeshGlobal()
{
    MeshGlobalMeshingWidget* w = new MeshGlobalMeshingWidget();
    auto *node = new ProjectNode("网格划分", QUuid::createUuid().toString(), (BasePage*)w);
    m_gridNode->addChild(node);
}

void MeshModuleController::onMeshSingle()
{
    // auto* w = new MeshSingleMeshingWidget();
    QWidget* w = new QWidget();
}

void MeshModuleController::onMeshMapped()
{
    // auto* w = new MeshMappedMeshingWidget();
    QWidget* w = new QWidget();
}

void MeshModuleController::onMeshSweep()
{
    // auto* w = new MeshSweepMeshingWidget();
    QWidget* w = new QWidget();
}

void MeshModuleController::onCtrlVolume()
{
    // auto* w = new MeshVolumeControlWidget();
    QWidget* w = new QWidget();
}

void MeshModuleController::onCtrlSurface()
{
    // auto* w = new MeshFaceControlWidget();
    QWidget* w = new QWidget();
}

void MeshModuleController::onCtrlEdge()
{
    // auto* w = new MeshEdgeControlWidget();
    QWidget* w = new QWidget();
}

void MeshModuleController::onQualityStd()
{
    // auto* w = new MeshQualityStandardWidget();
    QWidget* w = new QWidget();
}

void MeshModuleController::onMeshGrouping()
{
    auto *w = new MeshGroupingWidget();
    auto *node = new ProjectNode("网格分组", QUuid::createUuid().toString(), w);
    m_gridNode->addChild(node);
}

void MeshModuleController::onMeshEdit()
{
    auto *w = new GridEditingWidget();
    m_gridEditingWidget = w;
    if (m_importedGrid && m_importedGrid->GetNumberOfCells() > 0) {
        w->setEditableGrid(m_importedGrid, m_sourceFileName);
    }

    auto *node = new ProjectNode("网格编辑", QUuid::createUuid().toString(), w);
    m_gridNode->addChild(node);
}

void MeshModuleController::displayCurrentGrid()
{
    if (!m_visualWidgetService || !m_hasGrid || !m_importedGrid)
        return;

    vtkSmartPointer<vtkUnstructuredGrid> lineGrid;
    vtkSmartPointer<vtkUnstructuredGrid> nonLineGrid;
    splitGridByDim(m_importedGrid, lineGrid, nonLineGrid);

    static const QString kMeshSurfId = QStringLiteral("mesh.imported.surface");
    static const QString kMeshLineId = QStringLiteral("mesh.imported.line");
    m_visualWidgetService->removeProp(kMeshSurfId, false);
    m_visualWidgetService->removeProp(kMeshLineId, false);

    if (nonLineGrid && nonLineGrid->GetNumberOfCells() > 0) {
        auto surfActor = createSurfaceActor(nonLineGrid);
        m_visualWidgetService->addProp(kMeshSurfId, surfActor,
                                       QStringLiteral("网格"),
                                       QStringLiteral("导入网格-面/体"),
                                       true, true);
        m_previewSurfaceGrid = nonLineGrid;
        m_previewSurfaceActor = surfActor;
        m_previewSurfaceMapper = vtkDataSetMapper::SafeDownCast(surfActor->GetMapper());
    } else {
        m_previewSurfaceGrid = nullptr;
        m_previewSurfaceActor = nullptr;
        m_previewSurfaceMapper = nullptr;
    }

    if (lineGrid && lineGrid->GetNumberOfCells() > 0) {
        auto lineActor = createLineActor(lineGrid);
        m_visualWidgetService->addProp(kMeshLineId, lineActor,
                                       QStringLiteral("网格"),
                                       QStringLiteral("导入网格-线"),
                                       true, true);
        m_previewLineGrid = lineGrid;
        m_previewLineActor = lineActor;
        m_previewLineMapper = vtkDataSetMapper::SafeDownCast(lineActor->GetMapper());
    } else {
        m_previewLineGrid = nullptr;
        m_previewLineActor = nullptr;
        m_previewLineMapper = nullptr;
    }

    if (m_previewSurfaceGrid) {
        m_previewGrid = m_previewSurfaceGrid;
        m_previewActor = m_previewSurfaceActor;
        m_previewMapper = m_previewSurfaceMapper;
    } else {
        m_previewGrid = m_previewLineGrid;
        m_previewActor = m_previewLineActor;
        m_previewMapper = m_previewLineMapper;
    }
}


vtkSmartPointer<vtkUnstructuredGrid> MeshModuleController::getVtkUnstructuredGrid()
{
    return m_previewSurfaceGrid;
}

void MeshModuleController::splitGridByDim(vtkUnstructuredGrid* src,
                                          vtkSmartPointer<vtkUnstructuredGrid>& outLine,
                                          vtkSmartPointer<vtkUnstructuredGrid>& outNonLine) const
{
    outLine = nullptr;
    outNonLine = nullptr;
    if (!src) return;

    auto lineIds = vtkSmartPointer<vtkIdList>::New();
    auto nonLineIds = vtkSmartPointer<vtkIdList>::New();

    const vtkIdType nCells = src->GetNumberOfCells();
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        vtkCell* c = src->GetCell(cid);
        if (!c) continue;

        const int dim = c->GetCellDimension(); // 1=线,2=面,3=体
        if (dim == 1) lineIds->InsertNextId(cid);
        else          nonLineIds->InsertNextId(cid);
    }

    if (lineIds->GetNumberOfIds() > 0)
    {
        auto ex = vtkSmartPointer<vtkExtractCells>::New();
        ex->SetInputData(src);
        ex->SetCellList(lineIds);
        ex->Update();
        outLine = vtkUnstructuredGrid::SafeDownCast(ex->GetOutput());
    }

    if (nonLineIds->GetNumberOfIds() > 0)
    {
        auto ex = vtkSmartPointer<vtkExtractCells>::New();
        ex->SetInputData(src);
        ex->SetCellList(nonLineIds);
        ex->Update();
        outNonLine = vtkUnstructuredGrid::SafeDownCast(ex->GetOutput());
    }
}

vtkSmartPointer<vtkActor> MeshModuleController::createSurfaceActor(vtkUnstructuredGrid* grid) const
{
    if (!grid) return nullptr;

    auto mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    mapper->SetInputData(grid);
    mapper->ScalarVisibilityOff();

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);

    auto* p = actor->GetProperty();
    p->SetColor(0.7, 0.7, 0.7);      // 灰色面
    p->EdgeVisibilityOn();           // 显示边
    p->SetEdgeColor(0.0, 0.0, 0.0);  // 黑色边
    p->SetLineWidth(1.0);

    return actor;
}

vtkSmartPointer<vtkActor> MeshModuleController::createLineActor(vtkUnstructuredGrid* grid) const
{
    if (!grid) return nullptr;

    auto mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    mapper->SetInputData(grid);
    mapper->ScalarVisibilityOff();

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);

    auto* p = actor->GetProperty();
    p->SetColor(0.0, 0.0, 0.0);   // 黑线
    p->SetLineWidth(2.0);
    p->LightingOff();             // 关键：避免线被光照“洗白/发虚”
    p->RenderLinesAsTubesOn();    // 可选但很推荐：更粗更好点选（VTK支持则生效）

    return actor;
}

vtkSmartPointer<vtkUnstructuredGrid> MeshModuleController::getVtkUnstructuredGridSolid()
{
    return m_previewSurfaceGrid;
}

vtkSmartPointer<vtkUnstructuredGrid> MeshModuleController::getVtkUnstructuredGridLine()
{
    return m_previewLineGrid;
}



bool MeshModuleController::autoGroupingBySpatialBins(int binCount, QString* errMsg)
{
    if (!m_importedGrid || m_importedGrid->GetNumberOfCells() <= 0) {
        if (errMsg) *errMsg = QStringLiteral("当前没有可分组的网格");
        return false;
    }
    if (binCount < 2) {
        if (errMsg) *errMsg = QStringLiteral("自动分组数至少为 2");
        return false;
    }

    const vtkIdType nCells = m_importedGrid->GetNumberOfCells();
    struct CellCenter { vtkIdType cid; double x; double y; double z; };
    QVector<CellCenter> centers;
    centers.reserve((int)nCells);

    double minV[3] = { std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max() };
    double maxV[3] = { -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max() };

    for (vtkIdType cid = 0; cid < nCells; ++cid) {
        auto *cell = m_importedGrid->GetCell(cid);
        if (!cell) continue;
        double b[6] = {0,0,0,0,0,0};
        cell->GetBounds(b);
        const double cx = 0.5 * (b[0] + b[1]);
        const double cy = 0.5 * (b[2] + b[3]);
        const double cz = 0.5 * (b[4] + b[5]);
        minV[0] = std::min(minV[0], cx); minV[1] = std::min(minV[1], cy); minV[2] = std::min(minV[2], cz);
        maxV[0] = std::max(maxV[0], cx); maxV[1] = std::max(maxV[1], cy); maxV[2] = std::max(maxV[2], cz);
        centers.push_back({cid, cx, cy, cz});
    }

    if (centers.isEmpty()) {
        if (errMsg) *errMsg = QStringLiteral("无法读取单元中心，自动分组失败");
        return false;
    }

    const double span[3] = { maxV[0] - minV[0], maxV[1] - minV[1], maxV[2] - minV[2] };
    int axis = 0;
    if (span[1] > span[axis]) axis = 1;
    if (span[2] > span[axis]) axis = 2;

    auto *matypeArr = vtkIntArray::SafeDownCast(m_importedGrid->GetCellData()->GetArray("MATYPE"));
    if (!matypeArr) {
        auto newMatype = vtkSmartPointer<vtkIntArray>::New();
        newMatype->SetName("MATYPE");
        newMatype->SetNumberOfComponents(1);
        newMatype->SetNumberOfTuples(nCells);
        newMatype->FillComponent(0, 0);
        m_importedGrid->GetCellData()->AddArray(newMatype);
        matypeArr = newMatype;
    }

    auto *groupArr = vtkStringArray::SafeDownCast(m_importedGrid->GetCellData()->GetAbstractArray("GroupName"));
    if (!groupArr) {
        auto newGroup = vtkSmartPointer<vtkStringArray>::New();
        newGroup->SetName("GroupName");
        newGroup->SetNumberOfValues(nCells);
        for (vtkIdType i = 0; i < nCells; ++i) newGroup->SetValue(i, "");
        m_importedGrid->GetCellData()->AddArray(newGroup);
        groupArr = newGroup;
    } else if (groupArr->GetNumberOfValues() != nCells) {
        groupArr->SetNumberOfValues(nCells);
    }

    const double axisMin = minV[axis];
    const double axisSpan = std::max(span[axis], 1e-12);

    for (const auto &c : centers) {
        const double coord = (axis == 0 ? c.x : (axis == 1 ? c.y : c.z));
        int bin = static_cast<int>(((coord - axisMin) / axisSpan) * binCount);
        if (bin >= binCount) bin = binCount - 1;
        if (bin < 0) bin = 0;

        const int matype = bin + 1;
        matypeArr->SetValue(c.cid, matype);
        groupArr->SetValue(c.cid, QString("AutoGroup_%1").arg(matype).toStdString());
    }

    m_importedGrid->GetCellData()->Modified();
    m_importedGrid->Modified();
    displayCurrentGrid();
    return true;
}

bool MeshModuleController::groupByCoordinateRange(double xmin, double xmax,
                                                  double ymin, double ymax,
                                                  double zmin, double zmax,
                                                  int matype,
                                                  const QString& groupName,
                                                  QString* errMsg)
{
    if (!m_importedGrid || m_importedGrid->GetNumberOfCells() <= 0) {
        if (errMsg) *errMsg = QStringLiteral("当前没有可分组的网格");
        return false;
    }
    if (matype <= 0) {
        if (errMsg) *errMsg = QStringLiteral("分组编号(MATYPE)必须大于 0");
        return false;
    }

    if (xmin > xmax) std::swap(xmin, xmax);
    if (ymin > ymax) std::swap(ymin, ymax);
    if (zmin > zmax) std::swap(zmin, zmax);

    const vtkIdType nCells = m_importedGrid->GetNumberOfCells();
    auto *matypeArr = vtkIntArray::SafeDownCast(m_importedGrid->GetCellData()->GetArray("MATYPE"));
    if (!matypeArr) {
        auto newMatype = vtkSmartPointer<vtkIntArray>::New();
        newMatype->SetName("MATYPE");
        newMatype->SetNumberOfComponents(1);
        newMatype->SetNumberOfTuples(nCells);
        newMatype->FillComponent(0, 0);
        m_importedGrid->GetCellData()->AddArray(newMatype);
        matypeArr = newMatype;
    }

    auto *groupArr = vtkStringArray::SafeDownCast(m_importedGrid->GetCellData()->GetAbstractArray("GroupName"));
    if (!groupArr) {
        auto newGroup = vtkSmartPointer<vtkStringArray>::New();
        newGroup->SetName("GroupName");
        newGroup->SetNumberOfValues(nCells);
        for (vtkIdType i = 0; i < nCells; ++i) newGroup->SetValue(i, "");
        m_importedGrid->GetCellData()->AddArray(newGroup);
        groupArr = newGroup;
    } else if (groupArr->GetNumberOfValues() != nCells) {
        groupArr->SetNumberOfValues(nCells);
    }

    int changed = 0;
    for (vtkIdType cid = 0; cid < nCells; ++cid) {
        auto *cell = m_importedGrid->GetCell(cid);
        if (!cell) continue;
        double b[6] = {0,0,0,0,0,0};
        cell->GetBounds(b);
        const double cx = 0.5 * (b[0] + b[1]);
        const double cy = 0.5 * (b[2] + b[3]);
        const double cz = 0.5 * (b[4] + b[5]);
        if (cx >= xmin && cx <= xmax && cy >= ymin && cy <= ymax && cz >= zmin && cz <= zmax) {
            matypeArr->SetValue(cid, matype);
            groupArr->SetValue(cid, groupName.toStdString());
            ++changed;
        }
    }

    if (changed == 0) {
        if (errMsg) *errMsg = QStringLiteral("范围内没有命中任何单元");
        return false;
    }

    m_importedGrid->GetCellData()->Modified();
    m_importedGrid->Modified();
    displayCurrentGrid();
    return true;
}
