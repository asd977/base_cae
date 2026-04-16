#pragma once

#include <QList>
#include <QString>

template <typename T> class vtkSmartPointer;

class vtkProp;
class vtkActor;
class vtkProp3D;
class vtkTransform;
class vtkRenderer;
class vtkRenderWindow;
class vtkRenderWindowInteractor;
class vtkLookupTable;
class vtkPolyData;
class vtkMapper;
class vtkScalarBarActor;

class VisualWidgetService
{
  public:
    virtual ~VisualWidgetService() = default;

    // ---- core access ----
    virtual void render() = 0;
    virtual void resetCamera(bool renderNow = true) = 0;

    virtual vtkRenderer* renderer() const = 0;
    virtual vtkRenderWindow* renderWindow() const = 0;
    virtual vtkRenderWindowInteractor* interactor() const = 0;

    // ---- prop registry ----
    virtual bool addProp(const QString& id, vtkSmartPointer<vtkProp> prop,
                         const QString& group = QString(), const QString& name = QString(),
                         bool resetCam = false, bool renderNow = true) = 0;

    virtual bool removeProp(const QString& id, bool renderNow = true) = 0;
    virtual void clearAll(bool renderNow = true) = 0;

    virtual vtkProp* getProp(const QString& id) const = 0;
    virtual QList<QString> allIds() const = 0;

    virtual bool setVisible(const QString& id, bool on, bool renderNow = true) = 0;
    virtual void setGroupVisible(const QString& group, bool on, bool renderNow = true) = 0;

    virtual bool setOpacity(const QString& id, double alpha, bool renderNow = true) = 0;
    virtual bool setTransform(const QString& id, vtkTransform* tr, bool renderNow = true) = 0;

    // ---- LUT & ScalarBar ----
    virtual void setLookupTable(vtkLookupTable* lut, bool applyToAllActors = false,
                                bool renderNow = true) = 0;

    virtual vtkLookupTable* lookupTable() const = 0;
    virtual vtkScalarBarActor* scalarBarActor() const = 0;

    virtual void showScalarBar(bool on, bool renderNow = true) = 0;
    virtual bool isScalarBarVisible() const = 0;
    virtual void setScalarBarTitle(const QString& title, bool renderNow = true) = 0;
    virtual void setScalarBarNumberOfLabels(int n, bool renderNow = true) = 0;

    // ---- helpers ----
    virtual bool applyLookupTableToActor(const QString& id, const QString& scalarArrayName,
                                         double rangeMin, double rangeMax, bool useRange,
                                         bool renderNow = true) = 0;

    virtual bool updateActorPolyData(const QString& id, vtkPolyData* poly,
                                     bool renderNow = true) = 0;

    // ---- view switch / curve ----
    virtual bool showCurveFromTraceFile(const QString& traceFilePath, const QString& xField,
                                        const QStringList& yFields,
                                        QString* errMsg = nullptr) = 0;
    virtual void showVisualizationView() = 0;
};
