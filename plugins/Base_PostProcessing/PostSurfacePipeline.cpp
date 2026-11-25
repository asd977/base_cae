#include "PostSurfacePipeline.h"
#include "PostTecplotUtils.h"
#include <QThread>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <vtkAlgorithm.h>
#include <vtkCallbackCommand.h>
#include <vtkCommand.h>
#include <vtkGeometryFilter.h>
#include <vtkPolyDataNormals.h>
#include <vtkPointData.h>
#include <exception>

namespace {
void observeAbort(vtkAlgorithm* algorithm,std::atomic_bool* cancelled)
{
    auto callback=vtkSmartPointer<vtkCallbackCommand>::New();
    callback->SetClientData(cancelled);
    callback->SetCallback([](vtkObject* object,unsigned long,void* client,void*) {
        if(static_cast<std::atomic_bool*>(client)->load())
            if(auto* filter=vtkAlgorithm::SafeDownCast(object))filter->SetAbortExecute(1);
    });
    algorithm->AddObserver(vtkCommand::ProgressEvent,callback);
}
PostPreparedSurface prepare(const QString& file,vtkUnstructuredGrid* grid,
                            const PostSurfaceOptions& options,std::atomic_bool* cancelled)
{
    PostPreparedSurface result;result.fileName=file;result.options=options;
    QElapsedTimer timer;timer.start();
    result.source=grid?vtkSmartPointer<vtkUnstructuredGrid>(grid):PostTecplotUtils::readGrid(file,&result.error);
    if(!result.source || cancelled->load())return result;
    result.sourceTime=result.source->GetMTime();
    auto geometry=vtkSmartPointer<vtkGeometryFilter>::New();
    geometry->SetInputData(result.source);geometry->PassThroughPointIdsOn();geometry->PassThroughCellIdsOn();
    geometry->SetRemoveGhostInterfaces(false);geometry->SetOutputPointsPrecision(vtkAlgorithm::DEFAULT_PRECISION);
    observeAbort(geometry,cancelled);geometry->Update();
    if(cancelled->load())return result;
    result.surface=vtkSmartPointer<vtkPolyData>::New();result.surface->ShallowCopy(geometry->GetOutput());
    if(options.normals && !result.surface->GetPointData()->GetNormals()) {
        auto normals=vtkSmartPointer<vtkPolyDataNormals>::New();normals->SetInputData(result.surface);
        normals->ComputePointNormalsOn();normals->ComputeCellNormalsOff();
        normals->SetSplitting(options.splitting);normals->SetFeatureAngle(options.angle);
        normals->AutoOrientNormalsOff();normals->ConsistencyOff();
        normals->NonManifoldTraversalOff();normals->FlipNormalsOff();
        normals->SetOutputPointsPrecision(vtkAlgorithm::DEFAULT_PRECISION);
        observeAbort(normals,cancelled);normals->Update();
        if(cancelled->load())return result;
        auto output=vtkSmartPointer<vtkPolyData>::New();output->ShallowCopy(normals->GetOutput());result.surface=output;
    }
    result.prepareMs=timer.elapsed();return result;
}
}

PostSurfacePipeline::PostSurfacePipeline(QObject* parent):QObject(parent)
{
    m_dispatch.setSingleShot(true);m_dispatch.setInterval(0);
    connect(&m_dispatch,&QTimer::timeout,this,[this]{dispatch();});
}
PostSurfacePipeline::~PostSurfacePipeline()
{
    cancel();if(m_thread)m_thread->wait();
}
bool PostSurfacePipeline::pending() const
{ return m_thread || !m_requests.isEmpty() || m_dispatch.isActive(); }
void PostSurfacePipeline::cancel()
{
    ++m_epoch;m_dispatch.stop();m_requests.clear();
    if(m_cancelled)m_cancelled->store(true);
    if(stateChanged)stateChanged();
}
const PostPreparedSurface* PostSurfacePipeline::find(vtkUnstructuredGrid* grid,const PostSurfaceOptions& options)
{
    if(!grid)return nullptr;
    for(int i=0;i<m_cache.size();++i)
        if(m_cache[i].source==grid && m_cache[i].sourceTime==grid->GetMTime() && m_cache[i].options==options) {
            if(i!=m_cache.size()-1)m_cache.append(m_cache.takeAt(i));
            return &m_cache.last();
        }
    return nullptr;
}
void PostSurfacePipeline::store(const PostPreparedSurface& result)
{
    if(!result.source || !result.surface)return;
    m_cache.append(result);
    const auto memory=[this]{unsigned long kb=0;for(const auto& entry:m_cache)
        kb+=entry.source->GetActualMemorySize()+entry.surface->GetActualMemorySize();return kb;};
    // A threshold display needs two simultaneous surfaces. Keep that working
    // set even when it exceeds the soft memory budget, avoiding eviction loops.
    while(m_cache.size()>2 && (m_cache.size()>4 || memory()>256*1024))m_cache.removeFirst();
}
void PostSurfacePipeline::requestFile(const QString& file,const PostSurfaceOptions& options)
{
    if(m_thread && m_running.epoch==m_epoch && m_running.file==file && m_running.options==options)return;
    cancel();m_requests.append({file,nullptr,0,options,m_epoch});m_dispatch.start();
    if(stateChanged)stateChanged();
}
void PostSurfacePipeline::request(vtkUnstructuredGrid* grid,const PostSurfaceOptions& options)
{
    if(!grid || find(grid,options))return;
    if(m_thread && m_running.epoch==m_epoch && m_running.grid==grid
       && m_running.time==grid->GetMTime() && m_running.options==options)return;
    for(const auto& request:m_requests)
        if(request.grid==grid && request.time==grid->GetMTime() && request.options==options)return;
    m_requests.append({{},grid,grid->GetMTime(),options,m_epoch});
    while(m_requests.size()>2)m_requests.removeFirst();
    m_dispatch.start();if(stateChanged)stateChanged();
}
void PostSurfacePipeline::dispatch()
{
    if(m_thread || m_requests.isEmpty())return;
    m_running=m_requests.takeFirst();const auto request=m_running;
    auto result=std::make_shared<PostPreparedSurface>();
    m_cancelled=std::make_shared<std::atomic_bool>(false);const auto cancelled=m_cancelled;
    auto* thread=QThread::create([request,result,cancelled] {
        try {*result=prepare(request.file,request.grid,request.options,cancelled.get());}
        catch(const std::exception& error){result->error=QString::fromUtf8(error.what());}
        catch(...){result->error=QStringLiteral("结果显示准备失败。");}
    });
    m_thread=thread;
    connect(thread,&QThread::finished,thread,&QObject::deleteLater);
    connect(QCoreApplication::instance(),&QCoreApplication::aboutToQuit,thread,[thread]{thread->wait();},Qt::DirectConnection);
    connect(thread,&QThread::finished,this,[this,result,cancelled,request] {
        m_thread=nullptr;m_cancelled.reset();m_running={};
        if(!cancelled->load() && request.epoch==m_epoch && result->source
           && result->sourceTime==result->source->GetMTime()) {
            store(*result);if(ready)ready(*result);
        } else if(!cancelled->load() && request.epoch==m_epoch && !result->error.isEmpty()) {
            if(ready)ready(*result);
        }
        if(!m_requests.isEmpty())m_dispatch.start();
        if(stateChanged)stateChanged();
    },Qt::QueuedConnection);
    thread->start();if(stateChanged)stateChanged();
}
