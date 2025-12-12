#pragma once

#include "BasePage.h"
#include "CustomComboBox.h"
#include <QListWidget>
#include <mesh/BoundaryConditions.h>

namespace Ui {
class ElectrodeSurfaceSettingWidget;
}

class ElectrodeSurfaceSettingWidget : public BasePage
{
    Q_OBJECT
public:
    explicit ElectrodeSurfaceSettingWidget(QWidget *parent = nullptr);
    ~ElectrodeSurfaceSettingWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

private slots:
    // UI 按钮
    void on_addPushButton_clicked();
    void on_removePushButton_clicked();

    // comboBox Hover 高亮
    void onComboItemHovered(int row);
    void onComboDropdownClosed();

    // listWidget Hover/选择 预删
    void onListItemEntered(QListWidgetItem *item);
    void onListItemSelectionChanged();

    // 二次电流分布 开关 & 模型切换
    void on_secondaryCheckBox_toggled(bool checked);
    void on_modelComboBox_currentIndexChanged(int index);

    // Tafel 子支路启用/禁用
    void on_useAnodicTafelCheckBox_toggled(bool checked);
    void on_useCathodicTafelCheckBox_toggled(bool checked);

private:
    Ui::ElectrodeSurfaceSettingWidget *ui;

    // 渲染
    void refreshRender();

    // 根据当前 secondaryCheckBox / modelComboBox 等更新 UI 显示与 enabled
    void updateModelUI();
    void updateTafelSubUI();

    // 从 BCSolverParam 回填二次电流分布 UI
    void loadSecondaryParams(const BCSolverParam &param);

    // 把 UI 中二次电流分布设置打包进 scalars
    void collectSecondaryScalars(std::map<std::string, double> &scalars) const;
};

