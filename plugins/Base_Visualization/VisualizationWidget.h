#pragma once

#include <QWidget>
#include <QHash>
#include <QString>
#include <QVBoxLayout>

#include <services/VisualWidgetService.h>

#include <QVTKOpenGLNativeWidget.h>

// VTK
#include <vtkSmartPointer.h>

class vtkGenericOpenGLRenderWindow;
class vtkRenderer;
class vtkLookupTable;
class vtkScalarBarActor;
class vtkAxesActor;
class vtkOrientationMarkerWidget;
class vtkProp;
class vtkMapper;
class vtkTransform;
class vtkPolyData;

class VisualizationWidget : public QWidget, public VisualWidgetService
{
    Q_OBJECT
public:
    explicit VisualizationWidget(QWidget* parent = nullptr);
    ~VisualizationWidget() override;

    // ---- VisualWidgetService ----
    void render() override;
    void resetCamera(bool renderNow = true) override;

    vtkRenderer* renderer() const override;
    vtkRenderWindow* renderWindow() const override;
    vtkRenderWindowInteractor* interactor() const override;

    bool addProp(const QString& id,
                 vtkSmartPointer<vtkProp> prop,
                 const QString& group = QString(),
                 const QString& name  = QString(),
                 bool resetCam = false,
                 bool renderNow = true) override;

    bool removeProp(const QString& id, bool renderNow = true) override;
    void clearAll(bool renderNow = true) override;

    vtkProp* getProp(const QString& id) const override;
    QList<QString> allIds() const override;

    bool setVisible(const QString& id, bool on, bool renderNow = true) override;
    void setGroupVisible(const QString& group, bool on, bool renderNow = true) override;

    bool setOpacity(const QString& id, double alpha, bool renderNow = true) override;
    bool setTransform(const QString& id, vtkTransform* tr, bool renderNow = true) override;

    void setLookupTable(vtkLookupTable* lut,
                        bool applyToAllActors = false,
                        bool renderNow = true) override;

    vtkLookupTable* lookupTable() const override;

    void showScalarBar(bool on, bool renderNow = true) override;
    bool isScalarBarVisible() const override;
    void setScalarBarTitle(const QString& title, bool renderNow = true) override;
    void setScalarBarNumberOfLabels(int n, bool renderNow = true) override;

    bool applyLookupTableToActor(const QString& id,
                                 const QString& scalarArrayName,
                                 double rangeMin,
                                 double rangeMax,
                                 bool useRange,
                                 bool renderNow = true) override;

    bool updateActorPolyData(const QString& id, vtkPolyData* poly, bool renderNow = true) override;

    void showOrientationMarker(bool on);

private:
    void initVtk();

    static vtkMapper* actorMapper(vtkProp* prop);

private:
    struct Item
    {
        QString id;
        QString name;
        QString group;
        vtkSmartPointer<vtkProp> prop;
        bool visible = true;
    };

private:
    QVTKOpenGLNativeWidget* m_vtkWidget = nullptr;

    vtkSmartPointer<vtkGenericOpenGLRenderWindow> m_renderWindow;
    vtkSmartPointer<vtkRenderer> m_renderer;

    vtkSmartPointer<vtkLookupTable> m_lut;
    vtkSmartPointer<vtkScalarBarActor> m_scalarBar;

    vtkSmartPointer<vtkAxesActor> m_axes;
    vtkSmartPointer<vtkOrientationMarkerWidget> m_omw;

    QHash<QString, Item> m_registry;
};
