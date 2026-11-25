#pragma once

#include <services/VisualWidgetService.h>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <vtkActor.h>
#include <vtkCallbackCommand.h>
#include <vtkCommand.h>
#include <vtkDataSetMapper.h>
#include <vtkProperty.h>
#include <vtkWeakPointer.h>
#include <cmath>
#include <memory>

// VTK 9.2 has no independent EdgeOpacity. A native wireframe actor shares the
// surface input connection; only that actor blends when edge opacity is < 1.
namespace PostMeshEdges
{
inline QString actorId(const QString& sourceId){return sourceId+QStringLiteral(".mesh-edges");}

struct Binding;
using Bindings=QHash<QString,std::shared_ptr<Binding>>;
inline QHash<VisualWidgetService*,Bindings>& bindings()
{
    static QHash<VisualWidgetService*,Bindings> value;return value;
}

inline QSet<VisualWidgetService*>& watchedServices()
{
    static QSet<VisualWidgetService*> value;return value;
}

inline void remove(VisualWidgetService* service,const QString& sourceId);

struct Binding:std::enable_shared_from_this<Binding>
{
    VisualWidgetService* service{};
    QPointer<QObject> serviceObject;
    bool guardedService{false};
    QString sourceId,id;
    vtkWeakPointer<vtkActor> source,edge;
    vtkWeakPointer<vtkMapper> mapper;
    vtkWeakPointer<vtkProperty> property,edgeProperty;
    vtkSmartPointer<vtkCallbackCommand> callback,deleteCallback;
    unsigned long sourceTag{},sourceDeleteTag{},edgeDeleteTag{},mapperTag{},propertyTag{},edgePropertyTag{};
    double opacity{1},width{1};
    bool syncing{false};
    bool detached{false};

    Binding(VisualWidgetService* value,const QString& srcId,vtkActor* actor,vtkActor* lines)
        :service(value),serviceObject(dynamic_cast<QObject*>(value)),guardedService(!serviceObject.isNull()),
         sourceId(srcId),id(actorId(srcId)),source(actor),edge(lines)
    {
    }
    ~Binding()
    {
        detach();
    }
    void install()
    {
        callback=vtkSmartPointer<vtkCallbackCommand>::New();callback->SetClientData(this);
        callback->SetCallback([](vtkObject*,unsigned long,void* data,void*){
            if(auto* binding=static_cast<Binding*>(data))binding->sync();
        });
        deleteCallback=vtkSmartPointer<vtkCallbackCommand>::New();deleteCallback->SetClientData(this);
        deleteCallback->SetCallback([](vtkObject* caller,unsigned long,void* data,void*){
            if(auto* binding=static_cast<Binding*>(data))binding->handleDeleted(caller);
        });
        if(source) {
            sourceTag=source->AddObserver(vtkCommand::ModifiedEvent,callback);
            sourceDeleteTag=source->AddObserver(vtkCommand::DeleteEvent,deleteCallback);
        }
        if(edge) {
            edgeDeleteTag=edge->AddObserver(vtkCommand::DeleteEvent,deleteCallback);
        }
    }
    void detach()
    {
        if(detached)return;
        detached=true;
        if(callback)callback->SetClientData(nullptr);
        if(deleteCallback)deleteCallback->SetClientData(nullptr);
        if(source && sourceTag)source->RemoveObserver(sourceTag);
        if(source && sourceDeleteTag)source->RemoveObserver(sourceDeleteTag);
        if(edge && edgeDeleteTag)edge->RemoveObserver(edgeDeleteTag);
        if(mapper && mapperTag)mapper->RemoveObserver(mapperTag);
        if(property && propertyTag)property->RemoveObserver(propertyTag);
        if(edgeProperty && edgePropertyTag)edgeProperty->RemoveObserver(edgePropertyTag);
        sourceTag=sourceDeleteTag=edgeDeleteTag=mapperTag=propertyTag=edgePropertyTag=0;
    }
    void handleDeleted(vtkObject* caller)
    {
        if(detached)return;
        auto keepAlive=shared_from_this();
        auto* svc=service;
        const bool alive=svc && (!guardedService || !serviceObject.isNull());
        const bool isSource=(caller && caller==source.GetPointer());
        const QString srcId=sourceId;
        detach();
        auto owner=bindings().find(svc);
        if(owner!=bindings().end()) {
            owner->remove(srcId);
            if(owner->isEmpty())bindings().erase(owner);
        }
        // Only remove the edge prop when the source was already detached from the
        // service; never mutate the service registry while its own destructor is
        // tearing down props.
        if(isSource && alive && svc->getProp(srcId)!=caller && svc->getProp(actorId(srcId)))
            svc->removeProp(actorId(srcId),false);
    }
    void sync()
    {
        if(detached || syncing || !service || (guardedService && !serviceObject))return;
        if(!source || !edge || service->getProp(sourceId)!=source.GetPointer() ||
           service->getProp(id)!=edge.GetPointer()) {
            auto keepAlive=shared_from_this();
            remove(service,sourceId);
            return;
        }
        syncing=true;
        auto* currentMapper=source->GetMapper();auto* currentProperty=source->GetProperty();
        auto* currentEdgeProperty=edge->GetProperty();
        if(mapper!=currentMapper) {
            if(mapper && mapperTag)mapper->RemoveObserver(mapperTag);
            mapper=currentMapper;mapperTag=mapper?mapper->AddObserver(vtkCommand::ModifiedEvent,callback):0;
        }
        if(property!=currentProperty) {
            if(property && propertyTag)property->RemoveObserver(propertyTag);
            property=currentProperty;propertyTag=property?property->AddObserver(vtkCommand::ModifiedEvent,callback):0;
        }
        if(edgeProperty!=currentEdgeProperty) {
            if(edgeProperty && edgePropertyTag)edgeProperty->RemoveObserver(edgePropertyTag);
            edgeProperty=currentEdgeProperty;
            edgePropertyTag=edgeProperty?edgeProperty->AddObserver(vtkCommand::ModifiedEvent,callback):0;
        }
        auto* edgeMapper=vtkDataSetMapper::SafeDownCast(edge->GetMapper());
        auto* input=mapper && mapper->GetNumberOfInputPorts()>0 && mapper->GetNumberOfInputConnections(0)>0
            ?mapper->GetInputConnection(0,0):nullptr;
        if(edgeMapper) {
            auto* previous=edgeMapper->GetNumberOfInputConnections(0)>0?edgeMapper->GetInputConnection(0,0):nullptr;
            if(previous!=input)edgeMapper->SetInputConnection(input);
            auto* clipping=mapper?mapper->GetClippingPlanes():nullptr;
            if(edgeMapper->GetClippingPlanes()!=clipping)edgeMapper->SetClippingPlanes(clipping);
            edgeMapper->ScalarVisibilityOff();
        }
        edge->SetUserMatrix(source->GetMatrix());
        const double targetOpacity=qBound(0.0,opacity*(property?property->GetOpacity():1.0),1.0);
        if(edgeProperty) {
            if(std::abs(edgeProperty->GetLineWidth()-width)>1.0e-6)
                edgeProperty->SetLineWidth(width);
            if(std::abs(edgeProperty->GetOpacity()-targetOpacity)>1.0e-6)
                edgeProperty->SetOpacity(targetOpacity);
        }
        service->setVisible(id,input && source->GetVisibility() && targetOpacity>0.0,false);
        syncing=false;
    }
};

inline void remove(VisualWidgetService* service,const QString& sourceId)
{
    if(!service)return;
    std::shared_ptr<Binding> removed;
    auto owner=bindings().find(service);
    if(owner!=bindings().end()) {
        removed=owner->take(sourceId);
        if(owner->isEmpty())bindings().erase(owner);
    }
    if(removed)removed->detach();
    if(service->getProp(actorId(sourceId)))service->removeProp(actorId(sourceId),false);
}

inline void prune(VisualWidgetService* service)
{
    if(!service)return;
    const auto map=bindings().value(service);
    for(auto it=map.cbegin();it!=map.cend();++it) {
        const auto& binding=it.value();
        if(!binding || !binding->source || !binding->edge ||
           service->getProp(it.key())!=binding->source.GetPointer() ||
           service->getProp(actorId(it.key()))!=binding->edge.GetPointer()) {
            remove(service,it.key());
        }
    }
}

inline void update(VisualWidgetService* service,const QString& id,vtkActor* actor,
                   bool enabled,double opacity,double width)
{
    if(!service || !actor)return;
    if(!enabled) {remove(service,id);return;}
    auto old=bindings().value(service).value(id);
    if(old && (old->source!=actor || !old->edge || service->getProp(actorId(id))!=old->edge.GetPointer())) {
        remove(service,id);old.reset();
    }
    if(!old) {
        auto mapper=vtkSmartPointer<vtkDataSetMapper>::New();mapper->ScalarVisibilityOff();
        auto edge=vtkSmartPointer<vtkActor>::New();edge->SetMapper(mapper);
        edge->PickableOff();edge->UseBoundsOff();
        edge->GetProperty()->SetRepresentationToWireframe();
        edge->GetProperty()->SetColor(0,0,0);edge->GetProperty()->LightingOff();
        edge->GetProperty()->SetLineWidth(width);
        // Set translucent base opacity before addProp so VisualizationWidget
        // immediately enables depth peeling in updateTransparencyRendering().
        edge->GetProperty()->SetOpacity(qBound(0.0,opacity,1.0));
        service->addProp(actorId(id),edge,QStringLiteral("post"),QStringLiteral("单元边线"),false,false);
        old=std::make_shared<Binding>(service,id,actor,edge);
        old->install();
        bindings()[service].insert(id,old);
        if(!watchedServices().contains(service)) {
            if(auto* serviceObj=dynamic_cast<QObject*>(service)) {
                watchedServices().insert(service);
                QObject::connect(serviceObj,&QObject::destroyed,serviceObj,[service]() {
                    watchedServices().remove(service);
                    auto owner=bindings().take(service);
                    for(auto& binding:owner) {
                        if(binding)binding->detach();
                    }
                });
            }
        }
    }
    old->opacity=opacity;old->width=width;old->sync();
}

inline void clear(VisualWidgetService* service)
{
    if(!service)return;
    const auto ids=bindings().value(service).keys();
    for(const auto& id:ids)remove(service,id);
    const QString suffix=QStringLiteral(".mesh-edges");
    for(const QString& id:service->allIds()) {
        if(id.endsWith(suffix))service->removeProp(id,false);
    }
}
}
