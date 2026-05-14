#include <EventManager.h>
#include <PluginInterface.h>
#include <PublicConstants.h>
#include <StyleManager.h>

#include <QApplication>

#include <Logger.h>
#include <QDir>
#include <QPluginLoader>
#include <QSurfaceFormat>

void onAppAboutToQuit()
{
    EventManager::instance().sendEvent(PublicConstants::kEventQuitApp);
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    Logger::installQtMessageBridge();
    qCInfo(lcApp) << "Launcher started.";
    StyleManager::applyStyleSheet(app, QString(":/resources/style/app.qss"));

    QString exePath = QCoreApplication::applicationDirPath();

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, &onAppAboutToQuit);

    QString pluginDirPath = QDir::cleanPath(exePath) + "/plugins";
    qCInfo(lcApp) << "pluginDirPath " << pluginDirPath;
    QDir pluginDir(pluginDirPath);
    if (!pluginDir.exists())
    {
        qCInfo(lcApp) << "Plugin directory does not exist:" << pluginDirPath;
        return -1;
    }

    QMap<QString, PluginInterface*> loadedPlugins;

    QStringList pluginFiles = pluginDir.entryList(QStringList() << "*.dll" << "*.so", QDir::Files);
    for (const QString& fileName : pluginFiles)
    {
        QString pluginPath = pluginDir.absoluteFilePath(fileName);
        QPluginLoader* loader = new QPluginLoader(pluginPath);
        if (!loader->load())
        {
            qCInfo(lcApp) << "Failed to load plugin" << fileName << ":" << loader->errorString();
            delete loader;
            continue;
        }

        QObject* plugin = loader->instance();
        if (!plugin)
        {
            qCInfo(lcApp) << "Failed to get instance for" << fileName << ":" << loader->errorString();
            delete loader;
            continue;
        }

        PluginInterface* pluginInterface = qobject_cast<PluginInterface*>(plugin);
        if (pluginInterface)
        {
            QString name = pluginInterface->pluginName();
            loadedPlugins[name] = pluginInterface;
            qCInfo(lcApp) << "Loaded plugin:" << name;
        }
        else
        {
            qCInfo(lcApp) << "Plugin" << fileName << "does not implement PluginInterface";
            delete loader;
        }
    }

    QStringList runOrder = {"UiFrame",
                            "ProjectManager",
                            "Output",
                            "MaterialManager",
                            "PreProcessing",
                            "Solver",
                            "PostProcessing",
                            /*"GlobalParameter",*/ "SurrogateModel",
                            "PhySicalField",
                            "BoundaryConditions",
                            "Visualization",
                            "MeshModuleController"}; //

    for (const QString& name : runOrder)
    {
        if (loadedPlugins.contains(name))
        {
            qCInfo(lcApp) << "Starting plugin:" << name;
            loadedPlugins[name]->start();
        }
        else
        {
            qCWarning(lcApp) << "Plugin with name" << name << "not found!";
        }
    }

    EventManager::instance().sendEvent(PublicConstants::kEventLoadComplete);
    return app.exec();
}
