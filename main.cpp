#include "mainwindow.h"

#include <QApplication>

#include "core/StyleManager.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    StyleManager::applyStyleSheet(a, QStringLiteral(":/resources/style/app.qss"));
    MainWindow w;
    w.show();
    return a.exec();
}
