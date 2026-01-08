#ifndef LOADSETTINGWIDGET_H
#define LOADSETTINGWIDGET_H

#include <BasePage.h>
#include <QHash>
#include <QVector>
#include <QWidget>

#include <vtkSmartPointer.h>

#include "IiinGridModel.h"

class VisualWidgetService;
class MeshModuleControllerService;
class vtkActor;
class vtkPointPicker;
class vtkRenderer;
class vtkUnstructuredGrid;

namespace Ui {
class LoadSettingWidget;
}

struct LoadVector
{
    double fx = 0.0;
    double fy = 0.0;
    double fz = 0.0;
};

class LoadSettingWidget : public BasePage
{
    Q_OBJECT

public:
    explicit LoadSettingWidget(QWidget *parent = nullptr);
    ~LoadSettingWidget();

    void beforeLoad() override;
    void beforeUnload() override;

    void onPickedPoint(vtkIdType pointId);

private:
    struct BodyLoad {
        int targetId = 0;
        bool byMaterial = true;
        LoadVector value;
    };

    struct SurfaceLoad {
        QVector<int> nodeIds;
        LoadVector value;
    };

    struct PointLoad {
        int nodeId = 0;
        LoadVector value;
    };

    struct TemperatureLoad {
        int nodeId = 0;
        double alfa = 0.0;
        double dt = 0.0;
    };

    struct SeepageLoad {
        int targetId = 0;
        bool byMaterial = true;
        LoadVector value;
    };

    void setupInteractor();
    void teardownInteractor();
    void loadMeshFromFile(const QString &fileName);
    void updateLoadMarkers();
    void setLoadDisplayVisible();
    void appendPickedNode(int nodeId);
    void clearPickedNodes();
    LoadVector parseVector(const QString &text) const;
    void saveLoadFile(const QString &fileName);

private slots:
    void on_buttonLoadMesh_clicked();
    void on_buttonUseCurrentMesh_clicked();
    void on_buttonLoadLoad_clicked();
    void on_buttonSaveLoad_clicked();
    void on_checkShowBody_toggled(bool checked);
    void on_checkShowSurface_toggled(bool checked);
    void on_checkShowPoint_toggled(bool checked);
    void on_checkShowTemperature_toggled(bool checked);
    void on_checkShowSeepage_toggled(bool checked);
    void on_checkEnablePicking_toggled(bool checked);
    void on_buttonApplyBody_clicked();
    void on_buttonApplySurface_clicked();
    void on_buttonApplyPoint_clicked();
    void on_buttonApplyTemperature_clicked();
    void on_buttonApplySeepage_clicked();
    void on_buttonClearPicked_clicked();

private:
    Ui::LoadSettingWidget *ui;

    VisualWidgetService *m_visualWidgetService = nullptr;
    MeshModuleControllerService *m_meshModuleService = nullptr;
    vtkSmartPointer<vtkRenderer> m_renderer;
    vtkSmartPointer<vtkUnstructuredGrid> m_grid;
    vtkSmartPointer<vtkActor> m_meshActor;
    vtkSmartPointer<vtkPointPicker> m_pointPicker;

    vtkSmartPointer<vtkActor> m_bodyActor;
    vtkSmartPointer<vtkActor> m_surfaceActor;
    vtkSmartPointer<vtkActor> m_pointActor;
    vtkSmartPointer<vtkActor> m_temperatureActor;
    vtkSmartPointer<vtkActor> m_seepageActor;

    bool m_enablePicking = false;
    QVector<int> m_pickedNodeIds;

    QString m_meshFileName;
    QString m_loadFileName;
    IiinGridModel m_model;

    QVector<BodyLoad> m_bodyLoads;
    QVector<SurfaceLoad> m_surfaceLoads;
    QVector<PointLoad> m_pointLoads;
    QVector<TemperatureLoad> m_temperatureLoads;
    QVector<SeepageLoad> m_seepageLoads;
};

#endif // LOADSETTINGWIDGET_H
