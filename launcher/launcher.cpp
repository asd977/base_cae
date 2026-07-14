#include <EventManager.h>
#include <PluginInterface.h>
#include <PublicConstants.h>
#include <StyleManager.h>

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QPluginLoader>
#include <QSurfaceFormat>
#include <QtGlobal>

void onAppAboutToQuit()
{
    EventManager::instance().sendEvent(PublicConstants::kEventQuitApp);
}

int main(int argc, char** argv)
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("POWERCHINA-GEOCAE"));
    app.setOrganizationName(QStringLiteral("POWERCHINA"));

    StyleManager::applyStyleSheet(app, QString(":/resources/style/app.qss"));

    QString exePath = QCoreApplication::applicationDirPath();

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, &onAppAboutToQuit);

    QString pluginDirPath = QDir::cleanPath(exePath) + "/plugins";
    qDebug() << "pluginDirPath " << pluginDirPath;
    QDir pluginDir(pluginDirPath);
    if (!pluginDir.exists())
    {
        qDebug() << "Plugin directory does not exist:" << pluginDirPath;
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
            qDebug() << "Failed to load plugin" << fileName << ":" << loader->errorString();
            delete loader;
            continue;
        }

        QObject* plugin = loader->instance();
        if (!plugin)
        {
            qDebug() << "Failed to get instance for" << fileName << ":" << loader->errorString();
            delete loader;
            continue;
        }

        PluginInterface* pluginInterface = qobject_cast<PluginInterface*>(plugin);
        if (pluginInterface)
        {
            QString name = pluginInterface->pluginName();
            loadedPlugins[name] = pluginInterface;
            qDebug() << "Loaded plugin:" << name;
        }
        else
        {
            qDebug() << "Plugin" << fileName << "does not implement PluginInterface";
            delete loader;
        }
    }

    // 可视化先启动，确保 ADS 的中央工作区先建立；随后模型树、设置和日志
    // 会稳定地落在左、右、下区域，而不是依赖旧布局文件纠正位置。
    QStringList runOrder = {"UiFrame",
                            "Visualization",
                            "ProjectManager",
                            "Output",
                            "MaterialManager",
                            "PreProcessing",
                            "Solver",
                            "PostProcessing",
                            /*"GlobalParameter",*/ "SurrogateModel",
                            "PhySicalField",
                            "BoundaryConditions",
                            "MeshModuleController"};

    for (const QString& name : runOrder)
    {
        if (loadedPlugins.contains(name))
        {
            qDebug() << "Starting plugin:" << name;
            loadedPlugins[name]->start();
        }
        else
        {
            qWarning() << "Plugin with name" << name << "not found!";
        }
    }

    EventManager::instance().sendEvent(PublicConstants::kEventLoadComplete);
    return app.exec();
}
