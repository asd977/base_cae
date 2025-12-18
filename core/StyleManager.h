#pragma once

#include <QApplication>
#include <QString>
#include <core_global.h>

class CORE_EXPORT StyleManager
{
public:
    static bool applyStyleSheet(QApplication &app, const QString &resourcePath);
};
