#pragma once

#include <QWidget>
#include <QString>

class VisualWidgetService;

namespace Ui
{
class PostDisplayControlWidget;
}

class PostDisplayControlWidget : public QWidget
{
    Q_OBJECT

  public:
    explicit PostDisplayControlWidget(VisualWidgetService* visualService,
                                      QWidget* parent = nullptr);
    ~PostDisplayControlWidget() override;

    void syncFromState();

  private:
    void syncRangeControls();
    void applyScalarBarAppearance(bool renderNow = true);
    void notifyRefresh(const QString& reason);

  private:
    Ui::PostDisplayControlWidget* ui{nullptr};
    VisualWidgetService* m_visualService{nullptr};
    bool m_syncing{false};
};
