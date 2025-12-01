#ifndef VISUALWIDGET_H
#define VISUALWIDGET_H

#include <QWidget>
#include <vtkActor2D.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <QDebug>
#include <services/PreProcessingService.h>
#include <services/UiFrameService.h>
#include <services/VisualWidgetService.h>
#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>
#include <vtkSmartPointer.h>
#include <vtkXMLUnstructuredGridReader.h>
#include <vtkUnstructuredGrid.h>
#include <vtkDataSetMapper.h>
#include <vtkQuad.h>
#include <vtkStructuredGrid.h>
//#include <vtkActor.h>
#include <vtkRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkLookupTable.h>
#include <vtkPointData.h>
#include <QDebug>
#include <vtkAutoInit.h>
#include <vtkFloatArray.h>
#include <vtkDoubleArray.h>
#include <vtkContourFilter.h>
#include <vtkPolyDataMapper.h>
#include <vtkCutter.h>
#include <vtkPlane.h>
#include <vtkLineSource.h>
#include <vtkProbeFilter.h>
#include <vtkTable.h>
#include <vtkIntArray.h>
#include <vtkChartXY.h>
#include <vtkContextView.h>
#include <vtkContextScene.h>
#include <vtkPlot.h>
#include <vtkAxesActor.h>
#include <vtkArrowSource.h>
#include <vtkGlyph3DMapper.h>
#include <vtkPointSource.h>
#include <vtkStreamTracer.h>
#include <vtkRungeKutta4.h>
#include <vtkArrayCalculator.h>
#include <vtkStreamTracer.h>
#include <vtkProperty.h>
#include <QThread>
#include <vtkOrientationMarkerWidget.h>
#include <vtkCaptionActor2D.h>
#include <vtkTextProperty.h>
#include "vtkSmartPointer.h"
#include "vtkMultiBlockDataSet.h"
#include "vtkPointData.h"
#include "vtkCellData.h"
#include "vtkDataSetMapper.h"
#include "vtkPolyDataMapper.h"
#include "vtkActor.h"
#include "vtkRenderWindow.h"
#include "vtkRenderer.h"
#include "vtkCellDataToPointData.h"
#include "vtkAppendFilter.h"
#include <QInputDialog>
#include <QDir>
#include <QString>
#include <vtkCellPicker.h>
#include <vtkRendererCollection.h>
#include <ServiceRegistry.h>
#include <vtkSphereSource.h>
#include <vtkTextActor.h>
#include <vtkCamera.h>
#include <vtkScalarBarWidget.h>
#include <vtkScalarBarActor.h>

#include "vtkWidget.h"

class chartCurveUseTimes;

namespace Ui {
class VisualWidget;
}

class VisualWidget : public QWidget
{
    Q_OBJECT
        
private slots:
    void resetCamera();
    void PositiveX();
    void NegativeX();
    void PositiveY();
    void NegativeY();
    void PositiveZ();
    void NegativeZ();
    void rotateCamera_cw();
    void rotateCamera_ccw();
    void resetCameraToSideView();

public:
    explicit VisualWidget(QWidget *parent = nullptr);
    ~VisualWidget();

    void addActor(vtkSmartPointer<vtkActor> actor);
    void addActor2D(vtkSmartPointer<vtkActor2D> actor);
    void addActor2D2(vtkSmartPointer<vtkScalarBarActor> actor);
    void removeAllActor();
    void removeAllActor2();
    
    void reFreash();
    void reFresh();
    void removeGridActor(vtkSmartPointer<vtkActor> actor);

    vtkSmartPointer<vtkRenderWindow> getrenderWindow();
    void changeShowType(int show_type);
    void setChart(chartCurveUseTimes * chart_use);
    
    void setScalarBar(vtkSmartPointer<vtkScalarBarActor> scalarBar);
    void setScalarBarWidgetWidgetOnOrOff(int i_value);
    
    void saveImage(QString file_name,int i_type);

protected:
    void showEvent(QShowEvent *event) override
    {
//        // 首次调用 show() 函数时执行的逻辑
//        qDebug()<<"update VisualWidget";
//        m_renderWindow->Render();

//        // 调用父类的 showEvent 函数，确保正常的事件处理
//        QWidget::showEvent(event);
    }
private:
    void initVTKWidget();

private:
    Ui::VisualWidget *ui;
    UiFrameService *m_uiFrameService;
    
    vtkWidget *m_vtkWidget;

    vtkSmartPointer<vtkRenderer> m_renderer;
    vtkSmartPointer<vtkRenderWindow> m_renderWindow;
    vtkSmartPointer<vtkRenderWindowInteractor> m_renderWindowInteractor;
    vtkSmartPointer<vtkScalarBarActor> m_scalarbar;
    vtkSmartPointer<vtkActor> m_mainActor;
    QVector<vtkSmartPointer<vtkActor> > m_tmpActor;
    vtkSmartPointer<vtkActor2D> m_scalarBar_actor;
    vtkSmartPointer<vtkActor> m_gridActor;
    vtkSmartPointer< vtkScalarBarWidget > m_scalarBarWidget;

    void setupButton();
    vtkSmartPointer<vtkRenderer> getRender();
    vtkSmartPointer<vtkRenderWindowInteractor> getInteractor();
    void removeActor(vtkSmartPointer<vtkActor> actor);

    void resetViewDir(float look_x, float look_y, float look_z, float up_x, float up_y, float up_z);
    void initCameraToolBarAction();
    void createOrientationMarkerWidget();
    void createScalarBarWidgetWidget();
};

#endif // VISUALWIDGET_H



