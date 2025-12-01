#ifndef POSTPROCESSINGWIDGET_H
#define POSTPROCESSINGWIDGET_H

#include <QWidget>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkScalarBarActor.h>
#include <vtkSmartPointer.h>
#include <vtkLookupTable.h>
#include <vtkDataSetMapper.h>

namespace Ui {
class PostProcessingWidget;
}

enum class ViewType
{
    FitView = 0,
    XPlusView,
    XMinusView,
    YPlusView,
    YMinusView,
    ZPlusView,
    ZMinusView,
};

class PostProcessingWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PostProcessingWidget(QWidget *parent = nullptr);
    ~PostProcessingWidget();

    QList<QAction *> getViewActions() const;
    void addActor(vtkSmartPointer<vtkActor> actor);

    vtkSmartPointer<vtkScalarBarActor> getScalarBarActor();
    vtkSmartPointer<vtkLookupTable> getLookupTable();
    void removeActor(vtkSmartPointer<vtkActor> actor);
    void render();
    vtkRenderWindow* getRenderWindow();
    vtkRenderer *getRender();
protected slots:
    void showEvent(QShowEvent *event);
private:

    void setView(ViewType type);
    void fitView();
    void setViewValue(double x1, double x2, double x3, double y1, double y2, double y3, double z1, double z2, double z3);
    void createOrientationMarkerWidget();

private:

    QAction* m_fitView{ nullptr };
    QAction* m_XplusView{ nullptr };
    QAction* m_XminView{ nullptr };
    QAction* m_YplusView{ nullptr };
    QAction* m_YminView{ nullptr };
    QAction* m_ZplusView{ nullptr };
    QAction* m_ZminView{ nullptr };
    QAction* m_perspective{ nullptr };
    QAction* m_configView{nullptr};
    vtkSmartPointer<vtkLookupTable> m_lookupTable;
    Ui::PostProcessingWidget *ui;

    vtkSmartPointer<vtkRenderer> m_render;
    vtkSmartPointer<vtkRenderWindow> m_renderWindow;
    vtkSmartPointer<vtkRenderWindowInteractor> m_renderWindowInteractor;
    vtkSmartPointer<vtkScalarBarActor> m_scalarBarActor;
    vtkSmartPointer<vtkDataSetMapper> m_mapper;


    void initScalarBar();
};

#endif // POSTPROCESSINGWIDGET_H
