#include "Logger.h"

#include <ServiceRegistry.h>
#include <services/OutputService.h>

#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>

Q_LOGGING_CATEGORY(lcApp, "base.app")
Q_LOGGING_CATEGORY(lcSolver, "base.solver")
Q_LOGGING_CATEGORY(lcProcess, "base.process")

namespace
{
QMutex g_logMutex;
QtMessageHandler g_prevHandler = nullptr;

void bridgeHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    const QString text = Logger::formatMessage(type, context, msg);

    if (auto* outputService = ServiceRegistry::instance().getService<OutputService>())
    {
        outputService->outputLog(text);
    }

    if (g_prevHandler)
    {
        g_prevHandler(type, context, msg);
    }
}
} // namespace

void Logger::installQtMessageBridge()
{
    QMutexLocker locker(&g_logMutex);
    if (!g_prevHandler)
    {
        g_prevHandler = qInstallMessageHandler(&bridgeHandler);
    }
}

QString Logger::formatMessage(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    QString level = "[INFO]";
    if (type == QtDebugMsg)
        level = "[DEBUG]";
    else if (type == QtWarningMsg)
        level = "[WARN]";
    else if (type == QtCriticalMsg || type == QtFatalMsg)
        level = "[ERROR]";

    QString location;
    if (context.file && context.line > 0)
    {
        location = QString(" (%1:%2)").arg(context.file).arg(context.line);
    }

    return QString("%1 %2%3").arg(level, msg, location);
}
