// ElectrolyteSettingWidget.h
#pragma once

#include "BasePage.h"
#include "CustomComboBox.h"
#include <QListWidget>

#include <mesh/BoundaryConditions.h>

namespace Ui {
class ElectrolyteSettingWidget;
}

class ElectrolyteSettingWidget : public BasePage
{
    Q_OBJECT
public:
    explicit ElectrolyteSettingWidget(QWidget *parent = nullptr);
    ~ElectrolyteSettingWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

private slots:
    void on_addPushButton_clicked();
    void on_removePushButton_clicked();

    // ComboBox 下拉框里 hover 的行（预选）
    void onVolumeItemHovered(int row);
    // ComboBox 下拉关闭（取消预选）
    void onVolumeDropdownClosed();

    // listWidget 中 hover item（预删）
    void onListItemEntered(QListWidgetItem *item);
    // listWidget 当前选择变化时，取消预删
    void onListItemSelectionChanged();

private:
    void refreshRender(); // 触发 VTK 刷新

private:
    Ui::ElectrolyteSettingWidget *ui;
};
