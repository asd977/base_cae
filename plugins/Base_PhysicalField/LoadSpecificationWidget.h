#pragma once

#include "BasePage.h"
#include "vtkObject.h"

#include <QSet>
#include <vtkSmartPointer.h>

class vtkUnstructuredGrid;
class vtkPolyData;
class vtkActor;
class vtkDataSetMapper;
class vtkCellPicker;
class vtkCallbackCommand;
class vtkRenderer;
class vtkRenderWindowInteractor;

namespace Ui { class LoadSpecificationWidget; }

class LoadSpecificationWidget : public BasePage
{
    Q_OBJECT
public:
    explicit LoadSpecificationWidget(QWidget *parent = nullptr);
    ~LoadSpecificationWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

private slots:
    void on_clickCheckBox_checkStateChanged(const Qt::CheckState &arg1);

private:
    void enableFacePick(bool on);

    void setupSurfaceAndPicker();
    void teardownSurfaceAndPicker();

    static void OnLeftButtonPress(vtkObject* caller, unsigned long eid, void* clientdata, void* calldata);
    void handlePick(int displayX, int displayY);

    void rebuildHighlight();

private:
    Ui::LoadSpecificationWidget *ui = nullptr;

    // services
    class VisualWidgetService* m_visual = nullptr;
    class MeshModuleControllerService* m_meshCtrl = nullptr;

    // input grid
    vtkSmartPointer<vtkUnstructuredGrid> m_volumeGrid;

    // render context (从 VisualWidgetService 获取)
    vtkRenderer* m_renderer = nullptr;
    vtkRenderWindowInteractor* m_interactor = nullptr;

    // surface actor (可选的外表面显示，方便点选)
    vtkSmartPointer<vtkPolyData> m_surface;
    vtkSmartPointer<vtkDataSetMapper> m_surfaceMapper;
    vtkSmartPointer<vtkActor> m_surfaceActor;

    // highlight (选中面片集合 -> 一个 actor)
    QSet<vtkIdType> m_selectedSurfaceCellIds;
    vtkSmartPointer<vtkDataSetMapper> m_highlightMapper;
    vtkSmartPointer<vtkActor> m_highlightActor;

    // picker + callback
    vtkSmartPointer<vtkCellPicker> m_picker;
    vtkSmartPointer<vtkCallbackCommand> m_leftPressCb;

    bool m_pickEnabled = false;
};
