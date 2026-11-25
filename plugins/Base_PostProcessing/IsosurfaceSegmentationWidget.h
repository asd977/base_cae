#pragma once
#include <BasePage.h>
#include <vtkSmartPointer.h>

#include <QString>
#include <QVariantMap>
#include <QWidget>

class VisualWidgetService;
class vtkUnstructuredGrid;
class vtkThreshold;
class vtkContourFilter;
class vtkPolyDataMapper;
class vtkDataSetMapper;
class vtkActor;
class vtkCellDataToPointData;

namespace Ui
{
class IsosurfaceSegmentationWidget;
}

class IsosurfaceSegmentationWidget : public BasePage
{
    Q_OBJECT
  public:
    explicit IsosurfaceSegmentationWidget(VisualWidgetService* postProcessingWidget,
                                          QWidget* parent = nullptr);
    ~IsosurfaceSegmentationWidget() override;
    void beforeLoad() override;
    void beforeUnload() override;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& object) override;

  private slots:
    void on_analysisComboBox_currentIndexChanged(int index);
    void on_scalarComboBox_currentIndexChanged(int index);
    void on_percentCheckBox_toggled(bool checked);
    void on_isoSlider_valueChanged(int v);
    void on_isoSpin_valueChanged(double v);
    void on_showLowCheckBox_toggled(bool checked);
    void on_showHighCheckBox_toggled(bool checked);
    void on_showInterfaceCheckBox_toggled(bool checked);
    void on_lowOpacitySlider_valueChanged(int v);
    void on_highOpacitySlider_valueChanged(int v);
    void on_ifaceOpacitySlider_valueChanged(int v);

    void on_stepComboBox_currentIndexChanged(int index);
    void on_resultDirComboBox_currentIndexChanged(int index);
    void on_resultTimeComboBox_currentIndexChanged(int index);

  private:
    // 数据
    void readTecplotLatest();
    bool readTecplot(const QString& file);
    void fillScalarCombo();
    void setIsoSpinRangeFromScalar();
    double currentScalarMin() const;
    double currentScalarMax() const;

    // 管线
    void buildPipelineIfNeeded();
    void updateIsoFromUI();        // 更新阈值 & 等值面
    void updateColoringFromUI();   // 共用 LUT，并设置点/元数组着色
    void updateVisibilityFromUI(); // 显隐与透明度
    void renderNow();

    // 生命周期辅助
    void attachActorsIfNeeded();   // 根据标志把 actor 挂回场景
    void detachActorsIfAttached(); // 仅移除，不销毁
    void applyPendingState();

  private:
    Ui::IsosurfaceSegmentationWidget* ui{};
    VisualWidgetService* m_visualService{};

    // 生命周期状态
    bool m_deferRender{false};
    bool m_loadedOnce{false};
    bool m_hasPendingState{false};
    QVariantMap m_pendingState;
    bool m_attachedLow{false};
    bool m_attachedHigh{false};
    bool m_attachedIso{false};

    // 数据
    vtkSmartPointer<vtkUnstructuredGrid> m_grid;
    QString m_dataPath;
    double m_scalarRange[2]{0.0, 1.0};

    // 分割：两侧 + 中间等值面
    vtkSmartPointer<vtkThreshold> m_threshLow;
    vtkSmartPointer<vtkThreshold> m_threshHigh;
    vtkSmartPointer<vtkDataSetMapper> m_mapperLow;
    vtkSmartPointer<vtkDataSetMapper> m_mapperHigh;
    vtkSmartPointer<vtkActor> m_actorLow;
    vtkSmartPointer<vtkActor> m_actorHigh;

    vtkSmartPointer<vtkCellDataToPointData> m_cellToPoint;
    vtkSmartPointer<vtkContourFilter> m_contourIso;
    vtkSmartPointer<vtkPolyDataMapper> m_mapperIso;
    vtkSmartPointer<vtkActor> m_actorIso;
    void loadFileName(QString dataPath);
    void refreshResultGroups();
};
