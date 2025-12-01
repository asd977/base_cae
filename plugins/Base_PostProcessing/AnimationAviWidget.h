#pragma once

#include <QWidget>
#include <QVector>
#include <QTimer>
#include <BasePage.h>

#include <vtkSmartPointer.h>

class PostProcessingWidget;
class vtkUnstructuredGrid;
class vtkDataSetMapper;
class vtkActor;

namespace Ui { class AnimationAviWidget; }

class AnimationAviWidget : public BasePage
{
    Q_OBJECT
public:
    explicit AnimationAviWidget(PostProcessingWidget* postProcessingWidget,
                                QWidget* parent = nullptr);
    ~AnimationAviWidget();

    void beforeLoad();
    void beforeUnload();

private slots:
    // 播放控制
    void on_playPauseButton_clicked();
    void on_prevButton_clicked();
    void on_nextButton_clicked();
    void on_frameSlider_valueChanged(int value);
    void on_fpsSpin_valueChanged(double v);
    void on_loopCheckBox_toggled(bool);

    // 显示
    void on_scalarComboBox_currentIndexChanged(int);
    void on_fixRangeCheckBox_toggled(bool);

    // 导出
    void on_saveAviButton_clicked();

    void on_loadPushButton_clicked();

private:
    struct FrameItem {
        int group = 0;
        int order = 0;
        QString filePath;
    };

    // 目录扫描 & 读帧
    void scanFrames();
    bool loadFrame(int index);                      // 载入“原始帧”（不含插帧）
    vtkSmartPointer<vtkUnstructuredGrid> loadGridFromFile(const QString& file);
    void fillScalarComboFromCurrentGrid();
    bool computeGlobalRange(const QString& scalar, double outRange[2]);

    // 渲染 & LUT
    void buildPipelineIfNeeded();
    void applyScalarAndLUT(const QString& scalar, const double range[2]);
    void updateFrameLabel();
    void renderNow();

    // 播放计时
    void startTimer();
    void stopTimer();
    void onTick();                                  // 计时器回调

    // 插帧（数据线性插值）
    bool canInterpolateBetween(int i, int j) const;
    vtkSmartPointer<vtkUnstructuredGrid> interpolateGrids(vtkUnstructuredGrid* g0,
                                                          vtkUnstructuredGrid* g1,
                                                          double alpha) const;

    // AVI 导出
    bool exportAVI(const QString& fileName, double fps);

private:
    Ui::AnimationAviWidget* ui {};
    PostProcessingWidget*   m_postProcessingWidget {};

    // 数据
    QString m_dataPath;
    QVector<FrameItem> m_frames;
    int     m_currentIndex {0};     // 当前原始帧索引
    int     m_subStep {0};          // 当前插帧子步 (0..interpCount)

    // 渲染
    vtkSmartPointer<vtkUnstructuredGrid> m_grid;
    vtkSmartPointer<vtkDataSetMapper>    m_mapper;
    vtkSmartPointer<vtkActor>            m_actor;

    // LUT（固定范围）
    double  m_fixedRange[2] {0.0, 1.0};
    bool    m_hasFixedRange {false};

    // 播放
    QTimer  m_timer;
    bool    m_isPlaying {false};
};
