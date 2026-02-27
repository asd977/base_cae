#pragma once
#include <QWidget>
#include <QString>
#include <QVector>
#include <BasePage.h>

#include <vtkSmartPointer.h>

class VisualWidgetService;
class vtkUnstructuredGrid;
class vtkContourFilter;
class vtkPolyDataNormals;
class vtkPolyDataMapper;
class vtkActor;
class vtkLookupTable;

namespace Ui { class IsosurfaceSettingsWidget; }

class IsosurfaceSettingsWidget : public BasePage
{
    Q_OBJECT
public:
    explicit IsosurfaceSettingsWidget(VisualWidgetService* postProcessingWidget,
                                      QWidget *parent = nullptr);
    ~IsosurfaceSettingsWidget() override;

    // 生命周期（首次才重置，其余只挂载/卸载）
    void beforeLoad();
    void beforeUnload();

private slots:
    // 数据与 UI
    void on_scalarComboBox_currentIndexChanged(int);
    void on_percentCheckBox_toggled(bool);
    void on_isoSlider_valueChanged(int);
    void on_isoSpin_valueChanged(double);
    void on_confirmPushButton_clicked();

    // 互斥：单/多等值面
    void on_singleIsoGroupBox_toggled(bool checked);
    void on_multiIsoGroupBox_toggled(bool checked);

    void on_stepComboBox_currentIndexChanged(int index);
private:
    /* ------- 内部助手 ------- */
    void readTecplot(const QString& file);
    void fillScalarCombo();
    void setIsoSpinRangeFromScalar();
    double scalarMin() const { return m_scalarRange[0]; }
    double scalarMax() const { return m_scalarRange[1]; }

    // LUT/着色
    vtkLookupTable* ensureLUT();
    void updateOverallColoring();
    void renderNow();

    // 生成值
    double currentSingleIsoFromUI() const;                 // 百分比/绝对值 -> 单值
    QVector<double> multiIsoValuesFromUI() const;          // N个等距值，含/不含端点

    // 结果构建
    void ensureSinglePipeline();        // 单等值面：若未创建则创建管线
    void updateSingleIsoRealtime();     // 单等值面：实时更新+显示
    void buildSingleIsoResult();        // （保留：供需要时复用）
    void buildMultiIsoResult();         // 多等值面：仅“确定”时调用

    // 显示管理
    void detachAllFromScene();
    void reattachAccordingToState();
    void hideMultiActorsFromScene();
    void clearMultiResults();

private:
    Ui::IsosurfaceSettingsWidget* ui {};
    VisualWidgetService*         m_visualService {};

    bool    m_loadedOnce {false};
    QString m_dataPath;

    // 数据
    vtkSmartPointer<vtkUnstructuredGrid> m_grid;
    double m_scalarRange[2]{0.0, 1.0};

    // 整体云图
    vtkSmartPointer<vtkActor> m_actorOverall;

    // 单等值面
    vtkSmartPointer<vtkContourFilter>  m_contourSingle;
    vtkSmartPointer<vtkPolyDataMapper> m_mapperSingle;
    vtkSmartPointer<vtkActor>          m_actorSingle;

    // 多等值面（多路）
    QVector<vtkSmartPointer<vtkContourFilter>>  m_multiContours;
    QVector<vtkSmartPointer<vtkPolyDataMapper>> m_multiMappers;
    QVector<vtkSmartPointer<vtkActor>>          m_multiActors;

    // 显示状态（用于切页恢复）
    bool m_singleDisplayed {false};
    bool m_multiDisplayed  {false};
    void loadFileName(QString dataPath);
    void readTecplotLatest();
};
