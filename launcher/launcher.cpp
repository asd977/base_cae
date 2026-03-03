#include <QApplication>
#include <QPluginLoader>
#include <QDir>
#include <QDebug>
#include <PluginInterface.h>
#include <EventManager.h>
#include <PublicConstants.h>
#include <StyleManager.h>
#include <QSurfaceFormat>

void onAppAboutToQuit()
{
    EventManager::instance().sendEvent(PublicConstants::kEventQuitApp);
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    qDebug()<<"asd";
     StyleManager::applyStyleSheet(app, QStringLiteral(":/resources/style/app.qss"));

    QString exePath = QCoreApplication::applicationDirPath();

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, &onAppAboutToQuit);

    QString pluginDirPath = QDir::cleanPath(exePath) + "/plugins";
    qDebug()<<"pluginDirPath "<<pluginDirPath;
    QDir pluginDir(pluginDirPath);
    if (!pluginDir.exists())
    {
        qDebug() << "Plugin directory does not exist:" << pluginDirPath;
        return -1;
    }

    QMap<QString, PluginInterface*> loadedPlugins;

    QStringList pluginFiles = pluginDir.entryList(QStringList() << "*.dll" << "*.so", QDir::Files);
    for (const QString& fileName : pluginFiles) {
        QString pluginPath = pluginDir.absoluteFilePath(fileName);
        QPluginLoader* loader = new QPluginLoader(pluginPath);
        if (!loader->load()) {
            qDebug() << "Failed to load plugin" << fileName << ":" << loader->errorString();
            delete loader;
            continue;
        }

        QObject* plugin = loader->instance();
        if (!plugin) {
            qDebug() << "Failed to get instance for" << fileName << ":" << loader->errorString();
            delete loader;
            continue;
        }

        PluginInterface* pluginInterface = qobject_cast<PluginInterface*>(plugin);
        if (pluginInterface) {
            QString name = pluginInterface->pluginName();
            loadedPlugins[name] = pluginInterface;
            qDebug() << "Loaded plugin:" << name;
        } else {
            qDebug() << "Plugin" << fileName << "does not implement PluginInterface";
            delete loader;
        }
    }

    QStringList runOrder = { "UiFrame","ProjectManager", "Output",
                            "MaterialManager","PreProcessing","Solver","PostProcessing",
                            /*"GlobalParameter",*/"SurrogateModel","PhySicalField","BoundaryConditions",
                            "Visualization","MeshModuleController" };//

    for (const QString& name : runOrder) {
        if (loadedPlugins.contains(name)) {
            qDebug() << "Starting plugin:" << name;
            loadedPlugins[name]->start();
        } else {
            qWarning() << "Plugin with name" << name << "not found!";
        }
    }

    EventManager::instance().sendEvent(PublicConstants::kEventLoadComplete);
    return app.exec();
}

