#include <QApplication>

#include "core/Logger.h"
#include "core/StyleManager.h"
#include "mainwindow.h"

int main(int argc, char* argv[])
{
    QApplication a(argc, argv);
    Logger::installQtMessageBridge();
    StyleManager::applyStyleSheet(a, QStringLiteral(":/resources/style/app.qss"));
    MainWindow w;
    w.show();
    return a.exec();
}
