#ifndef VECTORSETTINGWIDGET_H
#define VECTORSETTINGWIDGET_H

#include "PostProcessingWidget.h"

#include <BasePage.h>
#include <QWidget>
#include <vtkGlyph3D.h>
#include <vtkUnstructuredGrid.h>

namespace Ui {
class VectorSettingWidget;
}

class VectorSettingWidget : public BasePage
{
    Q_OBJECT

public:
    explicit VectorSettingWidget(PostProcessingWidget *postProcessingWidget,QWidget *parent = nullptr);
    ~VectorSettingWidget();

    // 生命周期钩子（按你原有接口保留）
    void beforeLoad();
    void beforeUnload();

    // 外部把 Tecplot 文件路径传进来；首次使用时会读取
    void setFileName(const QString &file);

private slots:
    void on_confirmPushButton_clicked();
    void on_scaleSlider_valueChanged(int value);

    void on_stepComboBox_currentIndexChanged(int index);
private:
    Ui::VectorSettingWidget *ui{nullptr};
    PostProcessingWidget* m_postProcessingWidget{nullptr};

    QString m_fileName;
    vtkSmartPointer<vtkUnstructuredGrid> m_grid;       // 合并后的网格
    vtkSmartPointer<vtkActor>            m_vectorActor; // 当前矢量 actor
    vtkSmartPointer<vtkGlyph3D>          m_glyph;       // 持有以便动态改缩放

    double m_scaleFactor{0.5}; // 对应 slider 值 / 100.0
    int    m_maskOnRatio{1};
    QString m_dataPath;


private:
    bool ensureDataLoaded();                 // 若未加载则读取 Tecplot 并合并 block
    void populateComponentCombos();          // 用点数据数组名填充下拉框
    bool buildVectorArraysFromSelections(vtkUnstructuredGrid* grid,
                                         double& magMin, double& magMax); // 组装 Vector/VectorMag
    void rebuildPipeline();                  // 根据 UI 构建/刷新 glyph 渲染
    void requestRender() const;              // 尽量请求一次重绘

    // 工具：按名从 PointData 取数组
    static vtkDataArray* arrayByName(vtkUnstructuredGrid* grid, const QString& name);
    void loadFileName(QString dataPath);
    double computeAutoBaseScale(vtkUnstructuredGrid *grid);
};

#endif // VECTORSETTINGWIDGET_H
