#include "MeshModuleController.h"

#include <ProjectNode.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <EventManager.h>
#include <GridEditingWidget.h>
#include <MeshGlobalMeshingWidget.h>
#include <PublicConstants.h>
#include <QMessageBox>
#include <qfiledialog.h>

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
    if (!m_hasModel) {
        QMessageBox::warning(nullptr,
                             QStringLiteral("未导入网格"),
                             QStringLiteral("请先导入网格后再进行编辑。"));
        return;
    }

    GridEditingWidget* w = new GridEditingWidget();
    w->setModel(m_currentModel);
    auto *node = new ProjectNode("网格编辑", QUuid::createUuid().toString(), (BasePage*)w);
    m_gridNode->addChild(node);
}
