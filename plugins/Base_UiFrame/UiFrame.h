#ifndef UIFRAME_H
#define UIFRAME_H

#include <DockManager.h>
#include <DockWidget.h>
#include <services/UiFrameService.h>

#include <QAction>
#include <QByteArray>
#include <QCloseEvent>
#include <QList>
#include <QPointer>

#include "SARibbonMainWindow.h"

class SARibbonBar;
class SARibbonMenu;
class SARibbonPannel;
class QActionGroup;
class QStackedWidget;

namespace ads
{
class CDockAreaWidget;
}

class UiFrame : public SARibbonMainWindow, public UiFrameService
{
    Q_OBJECT
  public:
    UiFrame(QWidget* parent = nullptr,
            SARibbonMainWindowStyles style = {
                static_cast<int>(SARibbonMainWindowStyleFlag::UseNativeFrame) |
                static_cast<int>(SARibbonMainWindowStyleFlag::UseRibbonMenuBar)});
    ~UiFrame() override;

    QAction* addWidget(QWidget* widget, QString name) override;
    void showWidgetInLogArea(QWidget* widget, QString name) override;
    void setWidgetVisible(QWidget* widget, bool visible) override;
    void restoreUiState();
    void storeUiState();

  protected:
    void closeEvent(QCloseEvent* event) override;

  private:
    // 如果 UiFrameService 里有这个虚函数，这个 override 不能删（但可以藏到
    // private）
    QAction* addWidgetForTabwidget(QWidget* widget, QString name) override;

  private:
    void initDockManager();
    void initSARibbon();

    void buildRibbonAppButton(); // 文件菜单
    void buildRibbonTabs();      // 其它标签页

    void setDockWidgetStyle();
    void updateAreaTabStyle(ads::CDockAreaWidget* area);
    void updateAllContainersTabStyle();
    void applyThemeToUi();
    void switchTheme(const QString& themeName);
    void updateThemeActionChecks();
    void updateThemedActionIcons();
    void registerDockToggleAction(QAction* action, const QString& name);
    void addDockActionToWindowPanel(QAction* toggleAction, const QString& name);
    void showAllDockWidgets();
    void resetDockLayout();
    void showAboutDialog();
    ads::DockWidgetArea preferDockArea(const QString& name) const;
    void captureDefaultDockState();
    void arrangeDefaultDockLayout();

  private:
    ads::CDockManager* m_dockManager = nullptr;
    ads::CDockAreaWidget* m_centerDockArea = nullptr;
    QByteArray m_defaultDockState;
    QPointer<ads::CDockWidget> m_modelDockWidget;
    QPointer<ads::CDockWidget> m_settingsDockWidget;
    QPointer<ads::CDockWidget> m_visualDockWidget;
    QPointer<ads::CDockWidget> m_physicalDockWidget;
    QPointer<ads::CDockWidget> m_logDockWidget;
    QPointer<ads::CDockWidget> m_logContextDockWidget;
    QPointer<QStackedWidget> m_logContextStack;
    QPointer<ads::CDockWidget> m_visualControlAnchorDockWidget;
    QList<QPointer<ads::CDockWidget>> m_visualControlDockWidgets;
    bool m_defaultLayoutArranged = false;

    SARibbonBar* m_ribbon = nullptr;
    SARibbonMenu* m_applicationMenu = nullptr;
    QActionGroup* m_themeActionGroup = nullptr;
    QAction* m_themeLightAction = nullptr;
    QAction* m_themeBlueAction = nullptr;
    QAction* m_themeDarkAction = nullptr;
    SARibbonPannel* m_windowPanel = nullptr;
    QList<QPointer<QAction>> m_dockToggleActions;
};

#endif // UIFRAME_H
