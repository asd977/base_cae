#ifndef LOGGER_H
#define LOGGER_H

#include <core_global.h>

#include <QLoggingCategory>
#include <QString>

Q_DECLARE_EXPORTED_LOGGING_CATEGORY(lcApp, CORE_EXPORT)
Q_DECLARE_EXPORTED_LOGGING_CATEGORY(lcSolver, CORE_EXPORT)
Q_DECLARE_EXPORTED_LOGGING_CATEGORY(lcProcess, CORE_EXPORT)

class CORE_EXPORT Logger
{
  public:
    static void installQtMessageBridge();
    static QString formatMessage(QtMsgType type, const QMessageLogContext& context,
                                 const QString& msg);
};

#endif // LOGGER_H
