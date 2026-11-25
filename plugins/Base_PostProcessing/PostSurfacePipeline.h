#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QList>
#include <QString>
#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>
#include <vtkPolyData.h>
#include <atomic>
#include <functional>
#include <memory>

class QThread;

struct PostSurfaceOptions
{
    bool normals{false};
    bool splitting{true};
    double angle{30.0};
    bool operator==(const PostSurfaceOptions& other) const
    {
        return normals==other.normals &&
            (!normals || (splitting==other.splitting && angle==other.angle));
    }
};

struct PostPreparedSurface
{
    QString fileName;
    QString error;
    vtkSmartPointer<vtkUnstructuredGrid> source;
    vtkMTimeType sourceTime{0};
    PostSurfaceOptions options;
    vtkSmartPointer<vtkPolyData> surface;
    qint64 prepareMs{0};
};

// One background preparation per page, with coalesced requests and bounded
// immutable display outputs. Geometry work is exclusively VTK native filters.
class PostSurfacePipeline : public QObject
{
public:
    explicit PostSurfacePipeline(QObject* parent=nullptr);
    ~PostSurfacePipeline() override;
    std::function<void(const PostPreparedSurface&)> ready;
    std::function<void()> stateChanged;
    const PostPreparedSurface* find(vtkUnstructuredGrid*,const PostSurfaceOptions&);
    void requestFile(const QString&,const PostSurfaceOptions&);
    void request(vtkUnstructuredGrid*,const PostSurfaceOptions&);
    void cancel();
    bool pending() const;
private:
    struct Request {
        QString file;
        vtkSmartPointer<vtkUnstructuredGrid> grid;
        vtkMTimeType time{0};
        PostSurfaceOptions options;
        quint64 epoch{0};
    };
    void dispatch();
    void store(const PostPreparedSurface&);
    QTimer m_dispatch;
    QList<Request> m_requests;
    QList<PostPreparedSurface> m_cache;
    QPointer<QThread> m_thread;
    Request m_running;
    std::shared_ptr<std::atomic_bool> m_cancelled;
    quint64 m_epoch{0};
};
