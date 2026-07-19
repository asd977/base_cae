#pragma once
#include <BasePage.h>
#include <vtkSmartPointer.h>

#include <QString>
#include <QWidget>
class VisualWidgetService;

class vtkRenderWindowInteractor;
class vtkRenderer;
class vtkCellPicker;
class vtkCommand;
class vtkActor;
class vtkMapper;
class vtkDataSet;
class vtkDataArray;
class vtkPointLocator;

namespace Ui
{
class ProbeSettingsWidget;
}

class ProbeSettingsWidget : public BasePage
{
    Q_OBJECT
  public:
    explicit ProbeSettingsWidget(VisualWidgetService* postProcessingWidget,
                                 QWidget* parent = nullptr);
    ~ProbeSettingsWidget() override;

    // 生命周期：首次才挂钩；切页卸载时只解绑观察者，不清数据
    void beforeLoad() override;
    void beforeUnload() override;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& object) override;

  private slots:
    void on_enableProbeCheckBox_toggled(bool checked);
    void on_clearPushButton_clicked();

  private:
    // 绑定/解绑交互回调
    bool ensureVisualService();
    void hookInteractor();
    void unhookInteractor();

    // 实际拾取与入表
    void handlePick(int displayX, int displayY);

    // 工具：从 actor/mapper 推断数据、数组名与关联
    vtkDataSet* datasetFromActor(vtkActor* actor) const;
    QString activeArrayNameFromMapper(vtkMapper* mapper) const;
    int arrayAssociationFromMapper(vtkMapper* mapper) const; // 0=points, 1=cells, -1=unknown
    QString fallbackArrayNameFromScalarBar() const;

    // 工具：标量返回原值，多分量数组返回模长。
    bool fetchScalarValue(vtkDataSet* ds, const QString& arrayName, int assoc, long long pointId,
                          long long cellId, double& outValue, int& outComponents) const;

    // UI
    void appendRow(const double pos[3], const QString& arrayName, const QString& assocStr,
                   double value);
    void setupTableIfNeeded();

  private:
    Ui::ProbeSettingsWidget* ui{};
    VisualWidgetService* m_visualService{};

    // 交互
    vtkSmartPointer<vtkCellPicker> m_picker;
    vtkSmartPointer<vtkCommand> m_clickObserver;
    vtkSmartPointer<vtkPointLocator> m_pointLocator;
    vtkDataSet* m_locatorDataSet{};
    bool m_hooked{false};
    bool m_hasPendingEnabledState{false};
    bool m_pendingEnabledState{false};

    // 行号
    int m_rowCounter{0};
};
