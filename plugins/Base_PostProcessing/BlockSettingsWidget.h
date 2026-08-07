#pragma once

#include <BasePage.h>
#include <vtkActor.h>
#include <vtkDataSetMapper.h>
#include <vtkPlane.h>
#include <vtkSmartPointer.h>
#include <vtkTableBasedClipDataSet.h>
#include <vtkUnstructuredGrid.h>

#include <QString>
#include <QVariantMap>
#include <QWidget>

class VisualWidgetService;
namespace Ui
{
class BlockSettingsWidget;
}

class BlockSettingsWidget : public BasePage
{
    Q_OBJECT
  public:
    explicit BlockSettingsWidget(VisualWidgetService* postProcessingWidget,
                                 QWidget* parent = nullptr);
    ~BlockSettingsWidget() override;

    // 生命周期
    void beforeLoad() override;
    void beforeUnload() override;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& object) override;

  private slots:
    void on_analysisComboBox_currentIndexChanged(int index);
    void on_scalarComboBox_currentIndexChanged(int index);
    void on_planeComboBox_currentIndexChanged(int index);
    void on_blockSlider_valueChanged(int value);
    void on_blockPositionSpin_valueChanged(double value);
    void on_flipCheckBox_toggled(bool checked);

    void on_stepComboBox_currentIndexChanged(int index);
    void on_resultDirComboBox_currentIndexChanged(int index);
    void on_resultTimeComboBox_currentIndexChanged(int index);

  private:
    // 数据加载（选取 data 目录里“最新”的 stress_strainX-Y.txt）
    void loadLatestData();
    bool readTecplot(const QString& fileName);

    // 管线与渲染
    void buildClipPipelineIfNeeded();
    void updatePlaneFromUI();
    void updateColoringFromUI();
    void renderNow();
    void applyPendingState();
    void updatePositionControls(double normalizedPosition);
    double currentNormalizedPosition() const;

  private:
    Ui::BlockSettingsWidget* ui{};
    VisualWidgetService* m_visualService{};

    // 生命周期状态
    bool m_loadedOnce{false}; // 是否已完成首次加载与建管线
    bool m_isAttached{false}; // 当前 actor 是否已挂到场景
    bool m_hasPendingState{false};
    QVariantMap m_pendingState;

    // 数据
    vtkSmartPointer<vtkUnstructuredGrid> m_grid;
    QString m_dataPath;
    double m_XRange[2]{}, m_YRange[2]{}, m_ZRange[2]{};

    // Clip 管线
    vtkSmartPointer<vtkPlane> m_plane;
    vtkSmartPointer<vtkTableBasedClipDataSet> m_clipper;
    vtkSmartPointer<vtkDataSetMapper> m_mapper;
    vtkSmartPointer<vtkActor> m_actor;
    void loadFileName(QString dataPath);
    void refreshResultGroups();
};
