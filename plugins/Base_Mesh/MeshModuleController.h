#pragma once
#include <QObject>
#include <services/MeshModuleControllerService.h>

#include "IiinGridModel.h"
#include "MeshGridIO.h"

class ProjectNode;

class MeshModuleController : public QObject, public MeshModuleControllerService
{
    Q_OBJECT
public:
    explicit MeshModuleController(QObject* parent = nullptr);

    void newProject();
    bool saveProject();
    bool loadProject();
    void closeProject();


private:
    void initAction();

    // --- slots (handlers) ---
    void onImportGrid();
    void onExportGrid();

    void onMeshGlobal();
    void onMeshSingle();
    void onMeshMapped();
    void onMeshSweep();

    void onCtrlVolume();
    void onCtrlSurface();
    void onCtrlEdge();
    void onQualityStd();
    void onMeshEdit();

private:
    ProjectNode* m_gridNode = nullptr;
    IiinGridModel m_currentModel;
    bool m_hasModel = false;
    MeshGridIO m_gridIO;
};
