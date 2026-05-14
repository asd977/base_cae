#include <EventManager.h>
#include <PluginInterface.h>
#include <PublicConstants.h>
#include <StyleManager.h>

#include <QApplication>

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

    qInfo() << "启动程序中...";
    StyleManager::applyStyleSheet(app, QString(":/resources/style/app.qss"));

    QString exePath = QCoreApplication::applicationDirPath();

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, &onAppAboutToQuit);

    QString pluginDirPath = QDir::cleanPath(exePath) + "/plugins";
    qInfo() << "插件目录:" << pluginDirPath;
    QDir pluginDir(pluginDirPath);
    if (!pluginDir.exists())
    {
        qCritical() << "插件目录不存在，程序无法继续:" << pluginDirPath;
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
            qWarning() << "插件加载失败:" << fileName << "原因:" << loader->errorString();
            delete loader;
            continue;
        }

        QObject* plugin = loader->instance();
        if (!plugin)
        {
            qWarning() << "插件实例化失败:" << fileName << "原因:" << loader->errorString();
            delete loader;
            continue;
        }

        PluginInterface* pluginInterface = qobject_cast<PluginInterface*>(plugin);
        if (pluginInterface)
        {
            QString name = pluginInterface->pluginName();
            loadedPlugins[name] = pluginInterface;
            qInfo() << "插件已加载:" << name;
        }
        else
        {
            qWarning() << "插件接口不兼容:" << fileName;
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
            qInfo() << "正在启动模块:" << name;
            loadedPlugins[name]->start();
        }
        else
        {
            qWarning() << "模块未找到，已跳过:" << name;
        }
    }

    qInfo() << "模块启动流程完成，进入主界面。";
    EventManager::instance().sendEvent(PublicConstants::kEventLoadComplete);
    return app.exec();
}
