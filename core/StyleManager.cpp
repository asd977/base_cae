#include "StyleManager.h"

#include <QDebug>
#include <QFile>

bool StyleManager::applyStyleSheet(QApplication &app, const QString &resourcePath)
{
    QFile styleFile(resourcePath);
    if (!styleFile.open(QFile::ReadOnly | QFile::Text))
    {
        qWarning() << "Unable to open style sheet" << resourcePath << styleFile.errorString();
        return false;
    }

    const QString styleSheet = QString::fromUtf8(styleFile.readAll());
//    app.setStyleSheet(styleSheet);
    return true;
}
