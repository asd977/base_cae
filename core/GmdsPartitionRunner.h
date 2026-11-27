#pragma once
#include <core_global.h>
#include <QObject>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantMap>
#include <memory>

// One bounded worker per page. Successes are cached for the same CAD and rules.
class CORE_EXPORT GmdsPartitionRunner final : public QObject
{
    Q_OBJECT
public:
    explicit GmdsPartitionRunner(QObject* parent=nullptr);
    ~GmdsPartitionRunner() override;
    bool start(const QByteArray& brep,const QJsonObject& request,int timeoutMs=6000);
    void cancel();
    bool running() const { return m_running; }
signals:
    void finished(const QVariantMap& result);
private:
    void complete(QVariantMap result);
    QProcess m_process;
    QTimer m_timeout;
    std::unique_ptr<QTemporaryDir> m_temp;
    QByteArray m_key,m_cachedKey;
    QVariantMap m_cached;
    QString m_stopReason;
    bool m_running=false;
    bool m_timedOut=false;
    quint64 m_generation=0;
};
