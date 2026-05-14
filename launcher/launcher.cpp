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
    qCInfo(lcApp) << "启动程序中...";
    StyleManager::applyStyleSheet(app, QString(":/resources/style/app.qss"));

    QString exePath = QCoreApplication::applicationDirPath();

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, &onAppAboutToQuit);

    QString pluginDirPath = QDir::cleanPath(exePath) + "/plugins";
    qCInfo(lcApp) << "插件目录:" << pluginDirPath;
    QDir pluginDir(pluginDirPath);
    if (!pluginDir.exists())
    {
        qCCritical(lcApp) << "插件目录不存在，程序无法继续:" << pluginDirPath;
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
            qCWarning(lcApp) << "插件加载失败:" << fileName << "原因:" << loader->errorString();
            delete loader;
            continue;
        }

        QObject* plugin = loader->instance();
        if (!plugin)
        {
            qCWarning(lcApp) << "插件实例化失败:" << fileName << "原因:" << loader->errorString();
            delete loader;
            continue;
        }

        PluginInterface* pluginInterface = qobject_cast<PluginInterface*>(plugin);
        if (pluginInterface)
        {
            QString name = pluginInterface->pluginName();
            loadedPlugins[name] = pluginInterface;
            qCInfo(lcApp) << "插件已加载:" << name;
        }
        else
        {
            qCWarning(lcApp) << "插件接口不兼容:" << fileName;
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
            qCInfo(lcApp) << "正在启动模块:" << name;
            loadedPlugins[name]->start();
        }
        else
        {
            qCWarning(lcApp) << "模块未找到，已跳过:" << name;
        }
    }

    qCInfo(lcApp) << "模块启动流程完成，进入主界面。";
    EventManager::instance().sendEvent(PublicConstants::kEventLoadComplete);
    return app.exec();
}
