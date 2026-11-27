#include "GmdsPartitionRunner.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QVector3D>
#include <algorithm>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

GmdsPartitionRunner::GmdsPartitionRunner(QObject* parent) : QObject(parent)
{
#ifdef Q_OS_WIN
    m_process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW; });
#endif
    m_timeout.setSingleShot(true);
    connect(&m_timeout,&QTimer::timeout,this,[this] {
        m_timedOut=true;
        m_stopReason=tr("GMDS 分析超时，本次候选未添加。"); m_process.kill();
    });
    connect(&m_process,&QProcess::readyReadStandardOutput,this,[this] { m_process.readAllStandardOutput(); });
    connect(&m_process,&QProcess::readyReadStandardError,this,[this] { m_process.readAllStandardError(); });
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e) {
        if(e==QProcess::FailedToStart) complete({{"valid",false},{"message",tr("无法启动 GMDS 分块进程：%1").arg(m_process.errorString())}});
    });
    connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus status) {
        if(!m_running) return;
        if(!m_stopReason.isEmpty()) { complete({{"valid",false},{"cancelled",true},{"timedOut",m_timedOut},{"message",m_stopReason}}); return; }
        QFile f(m_temp->filePath("result.json"));
        QJsonParseError error; QJsonDocument doc;
        if(f.open(QIODevice::ReadOnly) && f.size()<1024*1024) doc=QJsonDocument::fromJson(f.readAll(),&error);
        if(status!=QProcess::NormalExit || !doc.isObject() || error.error!=QJsonParseError::NoError) {
            complete({{"valid",false},{"message",tr("GMDS 未能完成此几何的分析，本次候选未添加。")}}); return;
        }
        auto result=doc.object().toVariantMap();
        if(code!=0) result["valid"]=false;
        QVariantList suggestions;
        for(auto value:result.value("suggestions").toList()) {
            auto map=value.toMap();
            for(const auto& key:{QStringLiteral("origin"),QStringLiteral("normal")}) {
                const auto a=map.value(key).toList();
                if(a.size()!=3) { complete({{"valid",false},{"message",tr("GMDS 返回了无效的切面参数。")}}); return; }
                map[key]=QVariant::fromValue(QVector3D(a[0].toFloat(),a[1].toFloat(),a[2].toFloat()));
            }
            suggestions.append(map);
        }
        result["suggestions"]=suggestions; complete(result);
    });
}
GmdsPartitionRunner::~GmdsPartitionRunner()
{
    m_timeout.stop(); m_process.disconnect(this);
    if(m_process.state()!=QProcess::NotRunning) { m_process.kill(); m_process.waitForFinished(1000); }
}
bool GmdsPartitionRunner::start(const QByteArray& brep,const QJsonObject& request,int timeoutMs)
{
    if(m_running) return false;
    m_running=true; m_stopReason.clear(); m_timedOut=false;
    const auto generation=++m_generation;
    QCryptographicHash hash(QCryptographicHash::Sha256); hash.addData(brep); hash.addData(QJsonDocument(request).toJson(QJsonDocument::Compact)); m_key=hash.result();
    if(m_key==m_cachedKey && !m_cached.isEmpty()) { QTimer::singleShot(0,this,[this,generation] { if(m_generation!=generation) return; auto r=m_cached; r["cacheHit"]=true; complete(r); }); return true; }
    m_temp=std::make_unique<QTemporaryDir>(QDir::tempPath()+"/cae-gmds-XXXXXX");
    auto fail=[this,generation] { QTimer::singleShot(0,this,[this,generation] { if(m_generation==generation) complete({{"valid",false},{"message",tr("无法写入 GMDS 临时文件。")}}); }); };
    if(!m_temp->isValid()) { fail(); return true; }
    QFile cad(m_temp->filePath("model.brep"));
    if(!cad.open(QIODevice::WriteOnly) || cad.write(brep)!=brep.size()) { fail(); return true; } cad.close();
    auto json=request; json["brep"]=cad.fileName();
    QFile input(m_temp->filePath("request.json")); const auto data=QJsonDocument(json).toJson();
    if(!input.open(QIODevice::WriteOnly) || input.write(data)!=data.size()) { fail(); return true; } input.close();
    m_process.setProgram(QDir(QCoreApplication::applicationDirPath()).filePath("CAE_GMDSWorker.exe"));
    m_process.setArguments({input.fileName(),m_temp->filePath("result.json")});
    m_timeout.start(std::clamp(timeoutMs,0,6000)); m_process.start(); return true;
}
void GmdsPartitionRunner::cancel()
{
    if(!m_running) return;
    m_stopReason=tr("已取消 GMDS 分块分析。");
    if(m_process.state()!=QProcess::NotRunning) m_process.kill();
    else complete({{"valid",false},{"cancelled",true},{"message",m_stopReason}});
}
void GmdsPartitionRunner::complete(QVariantMap result)
{
    if(!m_running) return;
    m_timeout.stop(); m_running=false;
    if(!m_stopReason.isEmpty()) result={{"valid",false},{"cancelled",true},{"timedOut",m_timedOut},{"message",m_stopReason}};
    if(result.value("valid").toBool()) { m_cachedKey=m_key; m_cached=result; }
    m_temp.reset(); emit finished(result);
}
