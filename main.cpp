#include <QApplication>

#include "core/StyleManager.h"
#include "mainwindow.h"

int main(int argc, char* argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);
#endif
    QApplication a(argc, argv);
    StyleManager::applyStyleSheet(a, QStringLiteral(":/resources/style/app.qss"));
    MainWindow w;
    w.show();
    return a.exec();
}
