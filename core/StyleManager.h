#pragma once

#include <core_global.h>

#include <QApplication>
#include <QString>

class CORE_EXPORT StyleManager
{
  public:
    static bool applyStyleSheet(QApplication& app, const QString& resourcePath);

    // Supported theme ids: light / blue / dark.
    static bool applyTheme(QApplication& app, const QString& themeName);
    static QString normalizeThemeName(const QString& themeName);
    static QString currentThemeName();
    static QString themeDisplayName(const QString& themeName);

  private:
    static QString themeResourcePath(const QString& themeName);
};
