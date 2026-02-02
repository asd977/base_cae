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

bool MeshModuleController::currentModel(IiinGridModel &model) const
{
    if (!m_hasModel)
        return false;
    model = m_currentModel;
    return true;
}

QSet<int> MeshModuleController::getAvailableMatypes() const
{
    return m_currentModel.allMatypes();
}

void MeshModuleController::highlightMatypes(const QSet<int> &matypes,
                                            int selectedMatype,
                                            int previewMatype,
                                            int preDeleteMatype)
{
    if (!m_gridEditingWidget) {
        if (!m_previewMapper || !m_visualWidgetService) {
            return;
        }

        const bool hasSelection = !matypes.isEmpty()
            || selectedMatype >= 0
            || previewMatype >= 0
            || preDeleteMatype >= 0;

        if (!hasSelection) {
            m_previewMapper->ScalarVisibilityOff();
            m_visualWidgetService->render();
            return;
        }

        QSet<int> allMatypes = m_currentModel.allMatypes();
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

        m_previewMapper->SetScalarModeToUseCellData();
        m_previewMapper->SelectColorArray("MATYPE");
        m_previewMapper->SetLookupTable(lut);
        m_previewMapper->UseLookupTableScalarRangeOn();
        m_previewMapper->ScalarVisibilityOn();
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

    if (fileName.isEmpty())
        return;

    QFileInfo fi(fileName);
    const QString ext = fi.suffix().toLower();

    IiinGridModel model;
    QString err;

    if (ext == "nas") {
        QMessageBox::warning(nullptr,
                             QStringLiteral("不支持的格式"),
                             QStringLiteral("当前暂不支持 .nas 格式"));
        return;
    } else if (ext == "flac3d" || ext == "f3grid") {
        if (!m_gridIO.importFlac3D(fileName, model, &err)) {
            QMessageBox::warning(nullptr,
                                 QStringLiteral("导入失败"),
                                 err.isEmpty() ? QStringLiteral("Flac3D 网格导入失败") : err);
            return;
        }
    } else if (ext == "inp") {
        if (!m_gridIO.importInp(fileName, model, &err)) {
            QMessageBox::warning(nullptr,
                                 QStringLiteral("导入失败"),
                                 err.isEmpty() ? QStringLiteral("Abaqus 网格导入失败") : err);
            return;
        }
    } else if (ext == "dat") {
        if (!m_gridIO.loadIiinFile(fileName, model, &err)) {
            QMessageBox::warning(nullptr,
                                 QStringLiteral("导入失败"),
                                 err.isEmpty() ? QStringLiteral("IIIN.DAT 导入失败") : err);
            return;
        }
    } else {
        QMessageBox::warning(nullptr,
                             QStringLiteral("不支持的格式"),
                             QStringLiteral("当前只支持 .nas / .inp / .flac3d / .f3grid"));
        return;
    }

    m_currentModel = model;
    m_hasModel = true;
    displayCurrentModel();
}

void MeshModuleController::onExportGrid()
{
    if (!m_hasModel) {
        QMessageBox::warning(nullptr,
                             QStringLiteral("无可导出的网格"),
                             QStringLiteral("请先导入网格后再导出。"));
        return;
    }

    QString defaultPath = m_currentModel.fileName().isEmpty()
                              ? QStringLiteral("IIIN.dat")
                              : m_currentModel.fileName();

    QString fileName = QFileDialog::getSaveFileName(nullptr,
                                                    QStringLiteral("导出 IIIN.DAT"),
                                                    defaultPath,
                                                    QStringLiteral("IIIN Grid (*.dat)"));
    if (fileName.isEmpty())
        return;

    QString err;
    if (!m_gridIO.exportIiinFile(fileName, m_currentModel, &err)) {
        QMessageBox::warning(nullptr,
                             QStringLiteral("导出失败"),
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

void MeshModuleController::displayCurrentModel()
{
    if (!m_visualWidgetService || !m_hasModel)
        return;

    auto grid = buildGridFromModel(m_currentModel);
    if (!grid || grid->GetNumberOfCells() == 0)
        return;

    vtkSmartPointer<vtkUnstructuredGrid> lineGrid;
    vtkSmartPointer<vtkUnstructuredGrid> nonLineGrid;
    splitGridByDim(grid, lineGrid, nonLineGrid);

    // 先删旧的两个 prop
    static const QString kMeshSurfId = QStringLiteral("mesh.imported.surface");
    static const QString kMeshLineId = QStringLiteral("mesh.imported.line");
    m_visualWidgetService->removeProp(kMeshSurfId, false);
    m_visualWidgetService->removeProp(kMeshLineId, false);

    // 1) 面/体
    if (nonLineGrid && nonLineGrid->GetNumberOfCells() > 0)
    {
        auto surfActor = createSurfaceActor(nonLineGrid);

        m_visualWidgetService->addProp(kMeshSurfId,
                                       surfActor,
                                       QStringLiteral("网格"),
                                       QStringLiteral("导入网格-面/体"),
                                       true,
                                       true);

        // 保存引用（如果你后续 pick 需要）
        m_previewSurfaceGrid  = nonLineGrid;
        m_previewSurfaceActor = surfActor;
        m_previewSurfaceMapper = vtkDataSetMapper::SafeDownCast(surfActor->GetMapper());
    }
    else
    {
        m_previewSurfaceGrid = nullptr;
        m_previewSurfaceActor = nullptr;
        m_previewSurfaceMapper = nullptr;
    }

    // 2) 线
    if (lineGrid && lineGrid->GetNumberOfCells() > 0)
    {
        auto lineActor = createLineActor(lineGrid);

        m_visualWidgetService->addProp(kMeshLineId,
                                       lineActor,
                                       QStringLiteral("网格"),
                                       QStringLiteral("导入网格-线"),
                                       true,
                                       true);

        m_previewLineGrid  = lineGrid;
        m_previewLineActor = lineActor;
        m_previewLineMapper = vtkDataSetMapper::SafeDownCast(lineActor->GetMapper());
    }
    else
    {
        m_previewLineGrid = nullptr;
        m_previewLineActor = nullptr;
        m_previewLineMapper = nullptr;
    }

    // 原先你还存了 m_previewGrid/m_previewActor/m_previewMapper
    // 如果旧代码依赖它们，你可以决定：
    // - 优先指向 nonLine（面/体）；
    // - 若没有 nonLine，则指向 line
    // if (m_previewSurfaceGrid)
    // {
    //     m_previewGrid  = m_previewSurfaceGrid;
    //     m_previewActor = m_previewSurfaceActor;
    //     m_previewMapper = m_previewSurfaceMapper;
    // }
    // else
    // {
        m_previewGrid  = m_previewLineGrid;
        m_previewActor = m_previewLineActor;
        m_previewMapper = m_previewLineMapper;
    // }
}

vtkSmartPointer<vtkUnstructuredGrid> MeshModuleController::getVtkUnstructuredGrid()
{
    return m_previewSurfaceGrid;
}



vtkSmartPointer<vtkUnstructuredGrid>
MeshModuleController::buildGridFromModel(const IiinGridModel &model) const
{
    const auto &nodes = model.nodes();
    auto &elems = model.elems();
    if (nodes.isEmpty() || elems.isEmpty())
        return nullptr;

    // --- points ---
    auto points = vtkSmartPointer<vtkPoints>::New();
    points->SetNumberOfPoints(nodes.size());

    auto nodeIdArray = vtkSmartPointer<vtkIntArray>::New();
    nodeIdArray->SetName("node_id");
    nodeIdArray->SetNumberOfComponents(1);
    nodeIdArray->SetNumberOfTuples(nodes.size());

    for (vtkIdType i = 0; i < static_cast<vtkIdType>(nodes.size()); ++i) {
        const Node &n = nodes[static_cast<int>(i)];
        points->SetPoint(i, n.x, n.y, n.z);
        nodeIdArray->SetValue(i, n.id);
    }

    auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    grid->SetPoints(points);
    grid->GetPointData()->AddArray(nodeIdArray);

    // --- cell arrays ---
    auto matypeArray = vtkSmartPointer<vtkIntArray>::New();
    matypeArray->SetName("MATYPE");
    matypeArray->SetNumberOfComponents(1);

    auto elemIdArray = vtkSmartPointer<vtkIntArray>::New();
    elemIdArray->SetName("elem_id");
    elemIdArray->SetNumberOfComponents(1);

    auto ketypeArray = vtkSmartPointer<vtkIntArray>::New();
    ketypeArray->SetName("KETYPE");
    ketypeArray->SetNumberOfComponents(1);

    auto layerArray = vtkSmartPointer<vtkIntArray>::New();
    layerArray->SetName("LAYER");
    layerArray->SetNumberOfComponents(1);

    auto partArray = vtkSmartPointer<vtkIntArray>::New();
    partArray->SetName("src_part");
    partArray->SetNumberOfComponents(1);

    auto addCell = [&](int vtkCellType,
                       const QVector<int>& localNodeLids,
                       const Elem& e,
                       int part) -> void
    {
        QVector<vtkIdType> cellPtIds;
        cellPtIds.reserve(localNodeLids.size());

        for (int lid : localNodeLids) {
            if (lid < 0 || lid >= e.nodeIds.size()) return;

            const int nid = e.nodeIds[lid];
            if (nid == 0) return; // 缺省/填充 0：说明这个位置没节点

            const int pidx = model.nodeIndexById(nid);
            if (pidx < 0) return;

            cellPtIds.push_back(static_cast<vtkIdType>(pidx));
        }

        if (cellPtIds.size() != localNodeLids.size())
            return;

        grid->InsertNextCell(vtkCellType,
                             static_cast<vtkIdType>(cellPtIds.size()),
                             cellPtIds.data());

        matypeArray->InsertNextValue(e.matype);
        elemIdArray->InsertNextValue(e.id);
        ketypeArray->InsertNextValue(e.ketype);
        layerArray->InsertNextValue(e.layer);
        partArray->InsertNextValue(part);
    };

    // 代理显示策略（先保证“能显示/能点选/不丢 elem_id”，高阶节点以后再升级成高阶 VTK cell）
    auto proxyPieces = [&](int ketype) -> QVector<QPair<int, QVector<int>>>
    {
        QVector<QPair<int, QVector<int>>> pieces;

        switch (ketype)
        {
        // ---------------- 1D ----------------
        case 1: case 10: case 21:
            pieces.push_back({VTK_LINE, {0,1}});
            break;

        case 2: // 3-node line: IIIN=[1,2,3] (2 is mid) -> VTK_QUADRATIC_EDGE=[1,3,2]
            pieces.push_back({VTK_QUADRATIC_EDGE, {0,2,1}});
            break;

        // ---------------- 2D quad ----------------
        case 3: // 4-node quad: [1,2,3,4] matches VTK_QUAD
            pieces.push_back({VTK_QUAD, {0,1,2,3}});
            break;

        case 4: // 6-node quad (your picture)
            // VTK_QUADRATIC_LINEAR_QUAD = [c0,c1,c2,c3, mid01, mid23]
            // IIIN corners: 1,3,4,6 ; mids: 2(bottom),5(top)
            pieces.push_back({VTK_QUADRATIC_LINEAR_QUAD, {0,2,3,5,1,4}});
            break;

        case 5: // 8-node quad (your picture)
            // VTK_QUADRATIC_QUAD = [1,3,5,7,2,4,6,8]
            pieces.push_back({VTK_QUADRATIC_QUAD, {0,2,4,6,1,3,5,7}});
            break;

        case 29: // 9-node quad (your picture)
            // VTK_BIQUADRATIC_QUAD = [1,3,5,7,2,4,6,8,9]
            pieces.push_back({VTK_BIQUADRATIC_QUAD, {0,2,4,6,1,3,5,7,8}});
            break;

        case 11:
            pieces.push_back({VTK_QUAD, {0,1,2,3}});
            break;

        case 12: // 5-node quad, node5 is center -> split into 4 triangles
            pieces.push_back({VTK_TRIANGLE, {0,1,4}});
            pieces.push_back({VTK_TRIANGLE, {1,2,4}});
            pieces.push_back({VTK_TRIANGLE, {2,3,4}});
            pieces.push_back({VTK_TRIANGLE, {3,0,4}});
            break;

        // ---------------- 2D triangle ----------------
        case 31:
            pieces.push_back({VTK_TRIANGLE, {0,1,2}});
            break;

        case 32: // 6-node triangle (your picture)
            // corners: 1,3,5 ; mids: 2(1-3),4(3-5),6(5-1)
            // VTK_QUADRATIC_TRIANGLE = [c0,c1,c2, mid01, mid12, mid20]
            pieces.push_back({VTK_QUADRATIC_TRIANGLE, {0,2,4,1,3,5}});
            break;

        case 33: // 10-node triangle (your picture) -> proxy with corner triangle
            // corners: 1,4,7
            pieces.push_back({VTK_TRIANGLE, {0,3,6}});
            break;

        case 34: // 15-node triangle (your picture) -> proxy with corner triangle
            // corners: 1,5,9
            pieces.push_back({VTK_TRIANGLE, {0,4,8}});
            break;

        // ---------------- 3D ----------------
        case 35:
            pieces.push_back({VTK_TETRA, {0,1,2,3}});
            break;

        case 6:  // 8-node hex
        case 13: // 8-node unbounded hex
            pieces.push_back({VTK_HEXAHEDRON, {0,1,2,3,4,5,6,7}});
            break;

        case 36: // 14-node hex (your picture): corners are 1,2,3,4,10,11,12,13
            // local index: 1->0,2->1,3->2,4->3,10->9,11->10,12->11,13->12
            pieces.push_back({VTK_HEXAHEDRON, {0,1,2,3,9,10,11,12}});
            break;

        case 37: // 20-node hex (your picture) -> can use full VTK_QUADRATIC_HEXAHEDRON
            // corners: 1,3,5,7,13,15,17,19
            // mids: 2,4,6,8,14,16,18,20,9,10,11,12
            pieces.push_back({VTK_QUADRATIC_HEXAHEDRON,
                              {0,2,4,6,12,14,16,18,
                               1,3,5,7,
                               13,15,17,19,
                               8,9,10,11}});
            break;

        case 9:  // 8-node joint: show as two quads
        case 17: // doc says “transform to 9” -> for display you can also do two quads first
            pieces.push_back({VTK_QUAD, {0,1,2,3}});
            pieces.push_back({VTK_QUAD, {4,5,6,7}});
            break;

        default:
            break;
        }

        return pieces;
    };


    // --- build cells ---
    for (const Elem &e : elems) {
        auto pieces = proxyPieces(e.ketype);
        if (pieces.isEmpty()) continue;

        for (int i = 0; i < pieces.size(); ++i) {
            addCell(pieces[i].first, pieces[i].second, e, i);
        }
    }

    grid->GetCellData()->AddArray(matypeArray);
    grid->GetCellData()->SetScalars(matypeArray);

    grid->GetCellData()->AddArray(elemIdArray);
    grid->GetCellData()->AddArray(ketypeArray);
    grid->GetCellData()->AddArray(layerArray);
    grid->GetCellData()->AddArray(partArray);

    return grid;
}


int MeshModuleController::expectedNodeCountForKetype(int ketype) const
{
    switch (ketype) {
    case 1:  return 2;
    case 2:  return 3;
    case 3:  return 4;
    case 4:  return 6;
    case 5:  return 8;
    case 6:  return 8;
    case 7:  return 4;
    case 8:  return 6;
    case 9:  return 8;
    case 10: return 2;
    case 11: return 4;
    case 12: return 5;
    case 13: return 8;
    case 17: return 8;
    case 21: return 2;
    case 29: return 9;
    case 31: return 3;
    case 32: return 6;
    case 33: return 10;
    case 34: return 15;
    case 35: return 4;
    case 36: return 14;
    case 37: return 20;
    case 38: return 6;
    case 39: return 5;
    default:
        return m_currentModel.manod();
    }
}


int MeshModuleController::ketypeToVtkCellType(int ketype) const
{
    // 这里返回“显示代理类型”（不代表求解器真实高阶）
    switch (ketype) {
    case 1: case 2: case 10: case 21:
        return VTK_LINE;
    case 3: case 5: case 7: case 8: case 11: case 29:
        return VTK_QUAD;
    case 12:
        return VTK_POLYGON;
    case 31: case 32: case 33: case 34: case 4:
        return VTK_TRIANGLE;
    case 6: case 13: case 36: case 37:
        return VTK_HEXAHEDRON;
    case 9: case 17:
        // 实际 buildGridFromModel 是拆两片 QUAD
        return VTK_QUAD;
    case 35:
        return VTK_TETRA;
    case 38:
        return VTK_WEDGE;
    case 39:
        return VTK_PYRAMID;
    default:
        return -1;
    }
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

