#include <EventManager.h>
#include <PluginInterface.h>
#include <PublicConstants.h>
#include <StyleManager.h>

#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QEvent>
#include <QGroupBox>
#include <QIcon>
#include <QPluginLoader>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QSettings>
#include <QStyleOption>
#include <QSurfaceFormat>
#include <QTranslator>
#include <QLibraryInfo>
#include <QtGlobal>

#include <vtkObjectFactory.h>
#include <vtkOutputWindow.h>

class ApplicationVtkOutputWindow final : public vtkOutputWindow
{
  public:
    static ApplicationVtkOutputWindow* New();
    vtkTypeMacro(ApplicationVtkOutputWindow, vtkOutputWindow);

    void DisplayText(const char* text) override { writeToApplicationLog(QStringLiteral("信息"), text); }
    void DisplayErrorText(const char* text) override { writeToApplicationLog(QStringLiteral("错误"), text); }
    void DisplayWarningText(const char* text) override { writeToApplicationLog(QStringLiteral("警告"), text); }
    void DisplayGenericWarningText(const char* text) override
    {
        writeToApplicationLog(QStringLiteral("警告"), text);
    }
    void DisplayDebugText(const char* text) override { writeToApplicationLog(QStringLiteral("调试"), text); }

  protected:
    ApplicationVtkOutputWindow() = default;
    ~ApplicationVtkOutputWindow() override = default;

  private:
    static void writeToApplicationLog(const QString& level, const char* text)
    {
        const QString message = QString::fromUtf8(text ? text : "").trimmed();
        if (message.isEmpty())
            return;
        qWarning().noquote()
            << QStringLiteral("__RAW__[VTK %1] %2").arg(level, message);
    }
};

vtkStandardNewMacro(ApplicationVtkOutputWindow);

void onAppAboutToQuit()
{
    EventManager::instance().sendEvent(PublicConstants::kEventQuitApp);
}

class NeutralCheckStyle final : public QProxyStyle
{
  public:
    NeutralCheckStyle() : QProxyStyle(nullptr) {}

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                       const QWidget* widget = nullptr) const override
    {
        if (!option || !painter)
        {
            QProxyStyle::drawPrimitive(element, option, painter, widget);
            return;
        }

        const bool isCheck =
            element == PE_IndicatorCheckBox || element == PE_IndicatorItemViewItemCheck;
        const bool isRadio = element == PE_IndicatorRadioButton;
        if (!isCheck && !isRadio)
        {
            QProxyStyle::drawPrimitive(element, option, painter, widget);
            return;
        }

        const bool enabled = option->state.testFlag(State_Enabled);
        const bool hovered = option->state.testFlag(State_MouseOver);
        const bool pressed = option->state.testFlag(State_Sunken);
        const bool checked = option->state.testFlag(State_On);
        const bool partial = option->state.testFlag(State_NoChange);

        const QString theme =
            qApp ? qApp->property("uiTheme").toString().trimmed().toLower() : QString();

        QColor borderColor;
        QColor fillColor;
        QColor markColor;
        if (theme == QStringLiteral("dark"))
        {
            borderColor = !enabled   ? QColor(QStringLiteral("#4b535c"))
                          : hovered ? QColor(QStringLiteral("#91a0ad"))
                                    : QColor(QStringLiteral("#6f7a84"));
            fillColor = !enabled   ? QColor(QStringLiteral("#252a30"))
                        : pressed ? QColor(QStringLiteral("#353d45"))
                                  : QColor(QStringLiteral("#22272d"));
            markColor = enabled ? QColor(QStringLiteral("#79b4dc"))
                                : QColor(QStringLiteral("#737e87"));
        }
        else if (theme == QStringLiteral("blue"))
        {
            borderColor = !enabled   ? QColor(QStringLiteral("#afc4d2"))
                          : hovered ? QColor(QStringLiteral("#3f82b2"))
                                    : QColor(QStringLiteral("#739bb7"));
            fillColor = !enabled   ? QColor(QStringLiteral("#e8f1f6"))
                        : pressed ? QColor(QStringLiteral("#d2e5f2"))
                                  : QColor(QStringLiteral("#ffffff"));
            markColor = enabled ? QColor(QStringLiteral("#2878b0"))
                                : QColor(QStringLiteral("#9aaab6"));
        }
        else
        {
            borderColor = !enabled   ? QColor(QStringLiteral("#c5cbd0"))
                          : hovered ? QColor(QStringLiteral("#66727a"))
                                    : QColor(QStringLiteral("#8b959c"));
            fillColor = !enabled   ? QColor(QStringLiteral("#f2f4f5"))
                        : pressed ? QColor(QStringLiteral("#eef1f2"))
                                  : QColor(QStringLiteral("#ffffff"));
            markColor = enabled ? QColor(QStringLiteral("#30383d"))
                                : QColor(QStringLiteral("#9ea6ab"));
        }

        const int preferredSide = isRadio ? 15 : 16;
        const int side = qMin(preferredSide, qMin(option->rect.width(), option->rect.height()));
        QRect indicatorRect(0, 0, side, side);
        indicatorRect.moveCenter(option->rect.center());
        const QRectF outerRect = QRectF(indicatorRect).adjusted(0.65, 0.65, -0.65, -0.65);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(borderColor, 1.15));
        painter->setBrush(fillColor);

        if (isRadio)
        {
            painter->drawEllipse(outerRect);
            if (checked)
            {
                const qreal dotSide = qMax<qreal>(5.0, side * 0.42);
                QRectF dotRect(0.0, 0.0, dotSide, dotSide);
                dotRect.moveCenter(outerRect.center());
                painter->setPen(Qt::NoPen);
                painter->setBrush(markColor);
                painter->drawEllipse(dotRect);
            }
        }
        else
        {
            painter->drawRoundedRect(outerRect, 3.0, 3.0);
            if (checked)
            {
                const qreal x = indicatorRect.left();
                const qreal y = indicatorRect.top();
                QPainterPath checkPath;
                checkPath.moveTo(x + side * 0.22, y + side * 0.52);
                checkPath.lineTo(x + side * 0.43, y + side * 0.72);
                checkPath.lineTo(x + side * 0.79, y + side * 0.30);
                painter->setPen(
                    QPen(markColor, 1.65, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter->setBrush(Qt::NoBrush);
                painter->drawPath(checkPath);
            }
            else if (partial)
            {
                painter->setPen(QPen(markColor, 1.8, Qt::SolidLine, Qt::RoundCap));
                painter->drawLine(QPointF(indicatorRect.left() + side * 0.25,
                                          indicatorRect.top() + side * 0.50),
                                  QPointF(indicatorRect.left() + side * 0.75,
                                          indicatorRect.top() + side * 0.50));
            }
        }

        painter->restore();
    }

    int pixelMetric(PixelMetric metric, const QStyleOption* option = nullptr,
                    const QWidget* widget = nullptr) const override
    {
        if (metric == PM_IndicatorWidth || metric == PM_IndicatorHeight)
        {
            // QGroupBox 的标题区域比普通复选框更紧凑，使用 14px 可避免
            // 指示器被标题边界裁掉；普通复选框和项目视图保持 16px。
            if (qobject_cast<const QGroupBox*>(widget))
                return 14;
            return 16;
        }
        if (metric == PM_ExclusiveIndicatorWidth || metric == PM_ExclusiveIndicatorHeight)
            return 15;
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
};

class ComboBoxWheelGuard final : public QObject
{
  protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event && event->type() == QEvent::Wheel && qobject_cast<QComboBox*>(watched))
            return true;
        return QObject::eventFilter(watched, event);
    }
};

int main(int argc, char** argv)
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication app(argc, argv);
    auto* vtkOutput = ApplicationVtkOutputWindow::New();
    vtkOutputWindow::SetInstance(vtkOutput);
    vtkOutput->Delete();
    app.setWindowIcon(QIcon(QStringLiteral(":/resources/icon.svg")));
    app.setStyle(new NeutralCheckStyle);

    QTranslator qtChineseTranslator;
    const QString appTranslationDir =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("translations"));
    if (qtChineseTranslator.load(QStringLiteral("qtbase_zh_CN"), appTranslationDir) ||
        qtChineseTranslator.load(QStringLiteral("qtbase_zh_CN"),
                                 QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
    {
        app.installTranslator(&qtChineseTranslator);
    }
    ComboBoxWheelGuard comboBoxWheelGuard;
    app.installEventFilter(&comboBoxWheelGuard);
    app.setApplicationName(QStringLiteral("POWERCHINA-GEOCAE"));
    app.setOrganizationName(QStringLiteral("POWERCHINA"));

    const QString exePath = QCoreApplication::applicationDirPath();
    QSettings uiSettings(QDir(exePath).filePath(QStringLiteral("config/file.ini")),
                         QSettings::IniFormat);
    const QString initialTheme =
        uiSettings.value(QStringLiteral("General/uiTheme"), QStringLiteral("light")).toString();
    StyleManager::applyTheme(app, initialTheme);


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
        if (fileName.compare(QStringLiteral("Base_Slover.dll"), Qt::CaseInsensitive) == 0
            && pluginFiles.contains(QStringLiteral("Base_Solver.dll"), Qt::CaseInsensitive))
        {
            qWarning() << "Skipping legacy solver plugin:" << fileName;
            continue;
        }
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
