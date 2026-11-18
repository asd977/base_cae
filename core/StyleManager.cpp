#include "StyleManager.h"

#include <QApplication>
#include <QColor>
#include <QDebug>
#include <QFile>
#include <QPalette>
#include <QStyle>

namespace
{
void setPaletteColors(QPalette& palette, const QColor& window, const QColor& windowText,
                      const QColor& base, const QColor& alternateBase,
                      const QColor& button, const QColor& borderLight,
                      const QColor& borderMid, const QColor& borderDark,
                      const QColor& highlight, const QColor& highlightedText,
                      const QColor& disabledText, const QColor& placeholder,
                      const QColor& toolTipBase, const QColor& toolTipText)
{
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, windowText);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, alternateBase);
    palette.setColor(QPalette::ToolTipBase, toolTipBase);
    palette.setColor(QPalette::ToolTipText, toolTipText);
    palette.setColor(QPalette::Text, windowText);
    palette.setColor(QPalette::Button, button);
    palette.setColor(QPalette::ButtonText, windowText);
    palette.setColor(QPalette::BrightText, highlightedText);
    palette.setColor(QPalette::Highlight, highlight);
    palette.setColor(QPalette::HighlightedText, highlightedText);
    palette.setColor(QPalette::PlaceholderText, placeholder);
    palette.setColor(QPalette::Light, borderLight);
    palette.setColor(QPalette::Midlight, borderLight);
    palette.setColor(QPalette::Mid, borderMid);
    palette.setColor(QPalette::Dark, borderDark);
    palette.setColor(QPalette::Shadow, borderDark.darker(125));
    palette.setColor(QPalette::Link, highlight);
    palette.setColor(QPalette::LinkVisited, highlight.darker(115));

    palette.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    palette.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    palette.setColor(QPalette::Disabled, QPalette::PlaceholderText, disabledText);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, borderMid);
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledText);
}

QPalette paletteForTheme(const QString& themeName, const QStyle* style)
{
    QPalette palette = style ? style->standardPalette() : QPalette();

    if (themeName == QStringLiteral("dark"))
    {
        setPaletteColors(palette,
                         QColor(QStringLiteral("#1d2329")),
                         QColor(QStringLiteral("#e7edf2")),
                         QColor(QStringLiteral("#242b32")),
                         QColor(QStringLiteral("#2a323a")),
                         QColor(QStringLiteral("#293139")),
                         QColor(QStringLiteral("#4d5964")),
                         QColor(QStringLiteral("#3d4852")),
                         QColor(QStringLiteral("#20262c")),
                         QColor(QStringLiteral("#3b6f96")),
                         QColor(QStringLiteral("#ffffff")),
                         QColor(QStringLiteral("#78838d")),
                         QColor(QStringLiteral("#909ba5")),
                         QColor(QStringLiteral("#2d353d")),
                         QColor(QStringLiteral("#f2f5f7")));
    }
    else if (themeName == QStringLiteral("blue"))
    {
        setPaletteColors(palette,
                         QColor("#c4d9eb"), QColor("#173c5b"), QColor("#ffffff"),
                         QColor("#e8f1f8"), QColor("#dceaf5"), QColor("#f4f9fd"),
                         QColor("#9bbbd4"), QColor("#5e8fb5"), QColor("#245d89"),
                         QColor("#ffffff"), QColor("#879eaf"), QColor("#6e8ca3"),
                         QColor("#edf5fb"), QColor("#173c5b"));
    }
    else
    {
        setPaletteColors(palette,
                         QColor(QStringLiteral("#f0f2f4")),
                         QColor(QStringLiteral("#000000")),
                         QColor(QStringLiteral("#ffffff")),
                         QColor(QStringLiteral("#f7f8f9")),
                         QColor(QStringLiteral("#ffffff")),
                         QColor(QStringLiteral("#ffffff")),
                         QColor(QStringLiteral("#d7dcdf")),
                         QColor(QStringLiteral("#9da8af")),
                         QColor(QStringLiteral("#2f6f9f")),
                         QColor(QStringLiteral("#ffffff")),
                         QColor(QStringLiteral("#8a949b")),
                         QColor(QStringLiteral("#90999f")),
                         QColor(QStringLiteral("#ffffff")),
                         QColor(QStringLiteral("#000000")));
    }

    return palette;
}
} // namespace

bool StyleManager::applyStyleSheet(QApplication& app, const QString& resourcePath)
{
    QFile styleFile(resourcePath);
    if (!styleFile.open(QFile::ReadOnly | QFile::Text))
    {
        qWarning() << "Unable to open style sheet" << resourcePath << styleFile.errorString();
        return false;
    }

    const QString styleSheet = QString::fromUtf8(styleFile.readAll());
    app.setStyleSheet(styleSheet);
    return true;
}

QString StyleManager::normalizeThemeName(const QString& themeName)
{
    const QString value = themeName.trimmed().toLower();
    if (value == QStringLiteral("dark") || value == QStringLiteral("blue"))
        return value;
    return QStringLiteral("light");
}

QString StyleManager::themeResourcePath(const QString& themeName)
{
    const QString normalized = normalizeThemeName(themeName);
    if (normalized == QStringLiteral("dark"))
        return QStringLiteral(":/resources/style/app_dark.qss");
    if (normalized == QStringLiteral("blue"))
        return QStringLiteral(":/resources/style/app_blue.qss");
    return QStringLiteral(":/resources/style/app.qss");
}

QString StyleManager::currentThemeName()
{
    auto* app = qobject_cast<QApplication*>(QApplication::instance());
    return app ? normalizeThemeName(app->property("uiTheme").toString())
               : QStringLiteral("light");
}

QString StyleManager::themeDisplayName(const QString& themeName)
{
    const QString normalized = normalizeThemeName(themeName);
    if (normalized == QStringLiteral("dark"))
        return QStringLiteral("暗色");
    if (normalized == QStringLiteral("blue"))
        return QStringLiteral("深蓝");
    return QStringLiteral("浅灰");
}

bool StyleManager::applyTheme(QApplication& app, const QString& themeName)
{
    const QString normalized = normalizeThemeName(themeName);
    app.setProperty("uiTheme", normalized);
    app.setPalette(paletteForTheme(normalized, app.style()));
    return applyStyleSheet(app, themeResourcePath(normalized));
}
