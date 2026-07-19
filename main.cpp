#include <QApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QTranslator>

#include "core/StyleManager.h"
#include "mainwindow.h"

int main(int argc, char* argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);
#endif
    QApplication a(argc, argv);
    QTranslator qtChineseTranslator;
    const QString appTranslationDir =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("translations"));
    if (qtChineseTranslator.load(QStringLiteral("qtbase_zh_CN"), appTranslationDir) ||
        qtChineseTranslator.load(QStringLiteral("qtbase_zh_CN"),
                                 QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
    {
        a.installTranslator(&qtChineseTranslator);
    }
    StyleManager::applyStyleSheet(a, QStringLiteral(":/resources/style/app.qss"));
    MainWindow w;
    w.show();
    return a.exec();
}
