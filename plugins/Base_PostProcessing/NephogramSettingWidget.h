#ifndef NEPHOGRAMSETTINGWIDGET_H
#define NEPHOGRAMSETTINGWIDGET_H

#include <BasePage.h>
#include <QWidget>
#include <QString>
#include <QVariantMap>

#include <vtkSmartPointer.h>
#include <vtkActor.h>
#include <vtkDataSetMapper.h>
#include <vtkUnstructuredGrid.h>
#include <vtkRenderer.h>
#include <vtkLookupTable.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkCommand.h>

class PostProcessingWidget;
namespace Ui { class NephogramSettingWidget; }

class NephogramSettingWidget : public BasePage
{
    Q_OBJECT
public:
    explicit NephogramSettingWidget(PostProcessingWidget *postProcessingWidget, QWidget *parent = nullptr);
    ~NephogramSettingWidget();

    void beforeLoad() override;
    void beforeUnload() override;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject &obj) override;
    void selectGridNode(vtkObject *caller,
                        unsigned long eventId,
                        void *callData);

private slots:
    // 单图模式
    void on_pushButton_clicked();
    void on_stepComboBox_currentIndexChanged(int index);
    void on_scalarComboBox_currentIndexChanged(int index);

    // 对比模式 UI 槽
    void on_compareEnableCheckBox_toggled(bool checked);
    void on_leftStepComboBox_currentIndexChanged(int index);
    void on_rightStepComboBox_currentIndexChanged(int index);
    void on_splitSlider_valueChanged(int value);

    void on_pushButton_4_clicked();

private:
    void buildDimPhysIndex();
    void resetCellColors();
    void highlightDimPhys(int dimFilter, int physIdFilter);

private:
    // 单图辅助
    void ensureActorAttached();
    void syncScalarBarFromCurrent();
    void updateNephogram(const QString &selectedScalarName);
    void loadFileName(QString dataPath);
    void readTecplot(const QString &fileName);

    // 对比辅助
    void enterCompareMode();
    void exitCompareMode();
    void updateCompare();
    void updateViewports(double ratio); // 0~1
    void syncScalarBarForCompare(vtkDataArray* leftArr, vtkDataArray* rightArr);
    void fillCompareCombosFromSteps();
    vtkSmartPointer<vtkUnstructuredGrid> loadGridFromTecplot(const QString& fileName);

    void test();

private:
    Ui::NephogramSettingWidget *ui{nullptr};
    PostProcessingWidget *m_postProcessingWidget{nullptr};

    // —— 单图对象 ——
    vtkSmartPointer<vtkActor>            m_actor;
    vtkSmartPointer<vtkDataSetMapper>    m_mapper;
    vtkSmartPointer<vtkUnstructuredGrid> m_grid;

    // —— 对比对象 ——
    bool m_compareEnabled{false};
    double m_splitRatio{0.5}; // 0~1

    vtkSmartPointer<vtkRenderer>         m_leftRen;
    vtkSmartPointer<vtkRenderer>         m_rightRen;

    // 保存基础渲染器（持有 ScalarBar），用于分层覆盖
    vtkSmartPointer<vtkRenderer>         m_baseRenderer;

    vtkSmartPointer<vtkActor>            m_leftActor;
    vtkSmartPointer<vtkActor>            m_rightActor;

    vtkSmartPointer<vtkDataSetMapper>    m_leftMapper;
    vtkSmartPointer<vtkDataSetMapper>    m_rightMapper;

    vtkSmartPointer<vtkUnstructuredGrid> m_leftGrid;
    vtkSmartPointer<vtkUnstructuredGrid> m_rightGrid;

    // —— 状态/路径 ——
    QString m_dataPath;
    QString m_loadedFileName;
    QString m_currentArrayName;   // 仅用于渲染同步（不做持久化）
    bool m_initialized{false};    // 首次进入才读数据/填 step
    bool m_actorAttached{false};  // 避免重复 addActor

};

#endif
