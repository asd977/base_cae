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
    // 按钮
    void on_addPushButton_clicked();
    void on_removePushButton_clicked();

    // comboBox 下拉 hover（预选面：黄）
    void onComboItemHovered(int row);
    // comboBox 收起（取消预选）
    void onComboDropdownClosed();

    // listWidget hover 某个 item（预删面：红）
    void onListItemEntered(QListWidgetItem *item);
    // listWidget 选择变化时，取消预删
    void onListItemSelectionChanged();

private:
    void refreshRender(); // 调你自己的 VTK 刷新接口

private:
    Ui::ElectrodeSurfaceSettingWidget *ui;
};
