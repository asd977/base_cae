#ifndef UIFRAME_H
#define UIFRAME_H

#include <DockManager.h>
#include <DockWidget.h>
#include <services/UiFrameService.h>

#include <QAction>
#include <QByteArray>
#include <QCloseEvent>

#include "SARibbonMainWindow.h"

class SARibbonBar;
class SARibbonMenu;

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
    ads::DockWidgetArea preferDockArea(const QString& name) const;
    void captureDefaultDockState();

  private:
    ads::CDockManager* m_dockManager = nullptr;
    ads::CDockAreaWidget* m_centerDockArea = nullptr;
    QByteArray m_defaultDockState;

    SARibbonBar* m_ribbon = nullptr;
    SARibbonMenu* m_applicationMenu = nullptr;
};

#endif // UIFRAME_H
