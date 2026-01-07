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
        lut->Build();

        for (int i = 0; i <= maxMatype; ++i) {
            lut->SetTableValue(i, 0.75, 0.75, 0.75, 0.25);
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

        m_previewMapper->SetScalarModeToUseCellFieldData();
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

    auto mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    mapper->SetInputData(grid);
    mapper->ScalarVisibilityOff();

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->GetProperty()->EdgeVisibilityOn();
    actor->GetProperty()->SetLineWidth(1.0);

    static const QString kImportedMeshPropId = QStringLiteral("mesh.imported");
    m_visualWidgetService->removeProp(kImportedMeshPropId, false);
    m_visualWidgetService->addProp(kImportedMeshPropId,
                                   actor,
                                   QStringLiteral("网格"),
                                   QStringLiteral("导入网格"),
                                   true,
                                   true);

    m_previewMapper = mapper;
    m_previewActor = actor;
    m_previewGrid = grid;
}

vtkSmartPointer<vtkUnstructuredGrid>
MeshModuleController::buildGridFromModel(const IiinGridModel &model) const
{
    const auto &nodes = model.nodes();
    const auto &elems = model.elems();
    if (nodes.isEmpty() || elems.isEmpty())
        return nullptr;

    auto points = vtkSmartPointer<vtkPoints>::New();
    points->SetNumberOfPoints(nodes.size());

    for (vtkIdType i = 0; i < static_cast<vtkIdType>(nodes.size()); ++i) {
        const Node &n = nodes[static_cast<int>(i)];
        points->SetPoint(i, n.x, n.y, n.z);
    }

    auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    grid->SetPoints(points);

    auto matypeArray = vtkSmartPointer<vtkIntArray>::New();
    matypeArray->SetName("MATYPE");
    matypeArray->SetNumberOfComponents(1);

    for (const Elem &e : elems) {
        int expectedNodes = expectedNodeCountForKetype(e.ketype);
        int vtkCellType = ketypeToVtkCellType(e.ketype);
        if (vtkCellType < 0)
            continue;

        if (e.nodeIds.size() < expectedNodes)
            continue;

        QVector<vtkIdType> cellPtIds;
        cellPtIds.reserve(expectedNodes);
        for (int i = 0; i < expectedNodes; ++i) {
            int nid = e.nodeIds[i];
            if (nid <= 0 || nid > nodes.size())
                continue;
            cellPtIds.push_back(nid - 1);
        }

        if (cellPtIds.size() != expectedNodes)
            continue;

        grid->InsertNextCell(vtkCellType,
                             cellPtIds.size(),
                             cellPtIds.data());
        matypeArray->InsertNextValue(e.matype);
    }

    grid->GetCellData()->AddArray(matypeArray);
    return grid;
}

int MeshModuleController::expectedNodeCountForKetype(int ketype) const
{
    switch (ketype) {
    case 1:
    case 2:
    case 10:
    case 21:
        return 2;
    case 3:
    case 4:
    case 5:
    case 7:
    case 8:
    case 11:
    case 12:
    case 29:
        return 4;
    case 31:
    case 32:
    case 33:
    case 34:
        return 3;
    case 6:
    case 9:
    case 13:
    case 17:
    case 36:
    case 37:
        return 8;
    case 35:
        return 4;
    case 38:
        return 6;
    case 39:
        return 5;
    default:
        return m_currentModel.manod();
    }
}

int MeshModuleController::ketypeToVtkCellType(int ketype) const
{
    switch (ketype) {
    case 1:
    case 2:
    case 10:
    case 21:
        return VTK_LINE;
    case 3:
    case 4:
    case 5:
    case 7:
    case 8:
    case 11:
    case 12:
    case 29:
        return VTK_QUAD;
    case 31:
    case 32:
    case 33:
    case 34:
        return VTK_TRIANGLE;
    case 6:
    case 9:
    case 13:
    case 17:
    case 36:
    case 37:
        return VTK_HEXAHEDRON;
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
