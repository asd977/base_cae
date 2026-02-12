#pragma once
#include <QObject>
#include <QSet>
#include <QString>

#include <vtkSmartPointer.h>
#include <services/MeshModuleControllerService.h>

#include "MeshGridIO.h"

class ProjectNode;
class VisualWidgetService;
class vtkActor;
class vtkDataSetMapper;
class vtkUnstructuredGrid;
class GridEditingWidget;
class MeshGroupingWidget;

class MeshModuleController : public QObject, public MeshModuleControllerService
{
    Q_OBJECT
public:
    explicit MeshModuleController(QObject* parent = nullptr);

    void newProject();
    bool saveProject();
    bool loadProject();
    void closeProject();

    QSet<int> getAvailableMatypes() const override;
    void highlightMatypes(const QSet<int> &matypes,
                          int selectedMatype,
                          int previewMatype,
                          int preDeleteMatype) override;

    vtkSmartPointer<vtkUnstructuredGrid> getVtkUnstructuredGrid();
    vtkSmartPointer<vtkUnstructuredGrid> getVtkUnstructuredGridSolid();
    vtkSmartPointer<vtkUnstructuredGrid> getVtkUnstructuredGridLine();

    void importMshGrid(QString path);

    MshPickMap mshPickMapSolid();
    bool autoGroupingBySpatialBins(int binCount, QString* errMsg = nullptr) override;
    bool groupByCoordinateRange(double xmin, double xmax,
                                double ymin, double ymax,
                                double zmin, double zmax,
                                int matype,
                                const QString& groupName,
                                QString* errMsg = nullptr) override;

private:
    void initAction();

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
    void onMeshGrouping();

    void displayCurrentGrid(); // 只显示 m_importedGrid

    void splitGridByDim(vtkUnstructuredGrid* src,
                        vtkSmartPointer<vtkUnstructuredGrid>& outLine,
                        vtkSmartPointer<vtkUnstructuredGrid>& outNonLine) const;

    vtkSmartPointer<vtkActor> createSurfaceActor(vtkUnstructuredGrid* grid) const;
    vtkSmartPointer<vtkActor> createLineActor(vtkUnstructuredGrid* grid) const;

private:
    ProjectNode* m_gridNode = nullptr;

    // 彻底删掉 IiinGridModel，改为只存 vtk
    vtkSmartPointer<vtkUnstructuredGrid> m_importedGrid;
    bool m_hasGrid = false;
    QString m_sourceFileName;

    VisualWidgetService *m_visualWidgetService = nullptr;
    GridEditingWidget *m_gridEditingWidget = nullptr;

    vtkSmartPointer<vtkDataSetMapper> m_previewSurfaceMapper;
    vtkSmartPointer<vtkActor>         m_previewSurfaceActor;
    vtkSmartPointer<vtkUnstructuredGrid> m_previewSurfaceGrid;

    vtkSmartPointer<vtkDataSetMapper> m_previewLineMapper;
    vtkSmartPointer<vtkActor>         m_previewLineActor;
    vtkSmartPointer<vtkUnstructuredGrid> m_previewLineGrid;

    vtkSmartPointer<vtkDataSetMapper> m_previewMapper;
    vtkSmartPointer<vtkActor> m_previewActor;
    vtkSmartPointer<vtkUnstructuredGrid> m_previewGrid;
};
