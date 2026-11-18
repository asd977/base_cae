#ifndef PHYSICALFIELDSETTINGWIDGET_H
#define PHYSICALFIELDSETTINGWIDGET_H

#include <BasePage.h>
#include <QString>
#include <QVector>
#include <array>
#include <functional>

class QCheckBox;
class QLabel;
class QTableWidget;
class QWidget;
class VisualWidgetService;

namespace Ui { class PhysicalFieldSettingWidget; }

// 仅用于位移约束总览；节点仍由 PickManager 管理，不增加另一份持久数据。
struct DisplacementOverviewItem
{
    QString uuid;
    QString title;
    int ux = 1, uy = 1, uz = 1;
    QVector<int> nodeIds;
};

class PhysicalFieldSettingWidget : public BasePage
{
    Q_OBJECT
  public:
    explicit PhysicalFieldSettingWidget(QWidget* parent = nullptr);
    ~PhysicalFieldSettingWidget() override;
    void beforeLoad() override;
    void beforeUnload() override;
    void setDisplacementProvider(std::function<QVector<DisplacementOverviewItem>()> provider,
                                 std::function<void(const QString&)> openPage);

  private slots:
    void on_importIIIDButton_clicked();

  private:
    void refreshOverview();
    void updateOverviewOverlay();
    void clearOverviewOverlay(bool renderNow);
    QString selectedUuid() const;

    Ui::PhysicalFieldSettingWidget* ui;
    QTableWidget* m_table = nullptr;
    QCheckBox* m_showAll = nullptr;
    QLabel* m_info = nullptr;
    std::array<QCheckBox*, 8> m_colorChecks{};
    std::array<QLabel*, 8> m_colorCounts{};
    std::array<QWidget*, 8> m_colorCells{};
    VisualWidgetService* m_visual = nullptr;
    bool m_active = false;
    bool m_updatingTable = false;
    QVector<DisplacementOverviewItem> m_items;
    std::function<QVector<DisplacementOverviewItem>()> m_provider;
    std::function<void(const QString&)> m_openPage;
};

#endif // PHYSICALFIELDSETTINGWIDGET_H
