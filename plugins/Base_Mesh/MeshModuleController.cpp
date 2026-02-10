#include "MeshModuleController.h"

#include <ProjectNode.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <services/VisualWidgetService.h>
#include <EventManager.h>
#include <GridEditingWidget.h>
#include <MeshGlobalMeshingWidget.h>
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
    qDebug()<<"end1";
    m_visualWidgetService = ServiceRegistry::instance().getService<VisualWidgetService>();
    qDebug()<<"end2";
    initAction();
    qDebug()<<"end3";
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
    dialog.setNameFilter(QStringLiteral("Grid Files (*.nas *.inp *.flac3d *.f3grid *.DAT)"));
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
        grid = m_gridIO.importFlac3D(fileName, &err);
    } else if (ext == "inp") {
        grid = m_gridIO.importInp(fileName, &err);
    } else if (ext == "dat") {
        grid = m_gridIO.loadIiinFile(fileName, &err);
    } else {
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
    if (!m_gridIO.exportIiinFile(fileName, m_importedGrid, &err)) {
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

void MeshModuleController::onMeshEdit()
{
    auto *w = new GridEditingWidget();
    m_gridEditingWidget = w;
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

