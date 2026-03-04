#pragma once

#include <core_global.h>

#include <QApplication>
#include <QString>

class CORE_EXPORT StyleManager
{
  public:
    static bool applyStyleSheet(QApplication& app, const QString& resourcePath);
};
