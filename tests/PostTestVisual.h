#pragma once
#include <QtWidgets>
#include <services/VisualWidgetService.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkScalarBarActor.h>
#include <vtkCallbackCommand.h>
#include <vtkCommand.h>
class TestVisual : public VisualWidgetService
{
public:
    QHash<QString,vtkSmartPointer<vtkProp>> props;
    vtkNew<vtkRenderer> scene;
    vtkNew<vtkRenderWindow> window;
    vtkNew<vtkScalarBarActor> legend;
    TestVisual(){window->AddRenderer(scene);window->SetSize(950,700);window->SetOffScreenRendering(1);scene->SetBackground(.96,.97,.99);scene->AddActor2D(legend);
        auto callback=vtkSmartPointer<vtkCallbackCommand>::New();callback->SetClientData(this);
        callback->SetCallback([](vtkObject*,unsigned long,void* client,void*) {
            ++static_cast<TestVisual*>(client)->nativeRenderCalls;
        });
        window->AddObserver(vtkCommand::StartEvent,callback);
    }
    int renderCalls=0;
    int nativeRenderCalls=0;
    int totalRenderRequests() const {return renderCalls+nativeRenderCalls;}
    void render() override {++renderCalls;}
    void resetCamera(bool=true) override {scene->ResetCamera();}
    vtkRenderer* renderer() const override {return scene;}
    vtkRenderWindow* renderWindow() const override {return window;}
    vtkRenderWindowInteractor* interactor() const override {return nullptr;}
    bool addProp(const QString& id,vtkSmartPointer<vtkProp> prop,const QString& ={},const QString& ={},bool=false,bool=true) override
    {removeProp(id,false);props[id]=prop;scene->AddViewProp(prop);return true;}
    bool removeProp(const QString& id,bool=true) override
    {if(!props.contains(id))return false;scene->RemoveViewProp(props.take(id));return true;}
    void clearAll(bool=true) override {scene->RemoveAllViewProps();props.clear();}
    vtkProp* getProp(const QString& id) const override {return props.value(id);}
    QList<QString> allIds() const override {return props.keys();}
    bool setVisible(const QString& id,bool on,bool=true) override {auto* p=getProp(id);if(p)p->SetVisibility(on);return p;}
    void setGroupVisible(const QString&,bool,bool=true) override {}
    bool setOpacity(const QString&,double,bool=true) override {return false;}
    bool setTransform(const QString&,vtkTransform*,bool=true) override {return false;}
    void setLookupTable(vtkLookupTable*,bool=false,bool=true) override {}
    vtkLookupTable* lookupTable() const override {return nullptr;}
    vtkScalarBarActor* scalarBarActor() const override {return legend;}
    void showScalarBar(bool on,bool=true) override {legend->SetVisibility(on);}
    bool isScalarBarVisible() const override {return false;}
    void setScalarBarTitle(const QString&,bool=true) override {}
    void setScalarBarNumberOfLabels(int,bool=true) override {}
    bool applyLookupTableToActor(const QString&,const QString&,double,double,bool,bool=true) override {return false;}
    bool updateActorPolyData(const QString&,vtkPolyData*,bool=true) override {return false;}
    bool showCurveFromTraceFile(const QString&,const QString&,const QStringList&,QString* =nullptr) override {return false;}
    void showVisualizationView() override {}
    void setViewToolbarVisible(bool) override {}
    void setBrandingVisible(bool,bool=true) override {}
    bool isBrandingVisible() const override {return false;}
};
