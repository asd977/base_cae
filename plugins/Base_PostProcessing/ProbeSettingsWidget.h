#pragma once
#include <QWidget>
#include <QString>
#include <vtkSmartPointer.h>
#include <BasePage.h>
class VisualWidgetService;

class vtkRenderWindowInteractor;
class vtkRenderer;
class vtkCellPicker;
class vtkCommand;
class vtkActor;
class vtkMapper;
class vtkDataSet;
class vtkDataArray;

namespace Ui { class ProbeSettingsWidget; }

class ProbeSettingsWidget : public BasePage
{
    Q_OBJECT
public:
    explicit ProbeSettingsWidget(VisualWidgetService* postProcessingWidget,
                                 QWidget *parent = nullptr);
    ~ProbeSettingsWidget() override;

    // 生命周期：首次才挂钩；切页卸载时只解绑观察者，不清数据
    void beforeLoad();
    void beforeUnload();

private slots:
    void on_enableProbeCheckBox_toggled(bool checked);
    void on_clearPushButton_clicked();

private:
    // 绑定/解绑交互回调
    void hookInteractor();
    void unhookInteractor();

    // 实际拾取与入表
    void handlePick(int displayX, int displayY);

    // 工具：从 actor/mapper 推断数据、数组名与关联
    vtkDataSet* datasetFromActor(vtkActor* actor) const;
    QString     activeArrayNameFromMapper(vtkMapper* mapper) const;
    int         arrayAssociationFromMapper(vtkMapper* mapper) const; // 0=points, 1=cells, -1=unknown
    QString     fallbackArrayNameFromScalarBar() const;

    // 工具：从 DataSet 按点ID/单元ID读取值（首分量）
    bool fetchScalarValue(vtkDataSet* ds, const QString& arrayName, int assoc,
                          long long pointId, long long cellId,
                          double& outValue) const;

    // UI
    void appendRow(const double pos[3], const QString& arrayName, const QString& assocStr, double value);
    void setupTableIfNeeded();

private:
    Ui::ProbeSettingsWidget* ui {};
    VisualWidgetService*    m_visualService {};

    // 交互
    vtkSmartPointer<vtkCellPicker>    m_picker;
    vtkSmartPointer<vtkCommand>       m_clickObserver;
    bool                              m_hooked {false};

    // 行号
    int m_rowCounter {0};
};
