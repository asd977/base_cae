#ifndef NEPHOGRAMSETTINGWIDGET_H
#define NEPHOGRAMSETTINGWIDGET_H

#include <BasePage.h>
#include <vtkActor.h>
#include <vtkCommand.h>
#include <vtkDataSetMapper.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkLookupTable.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>

#include <QMap>
#include <QString>
#include <QVariantMap>
#include <QWidget>
#include <vector>

class VisualWidgetService;
class vtkBillboardTextActor3D;
namespace Ui
{
class NephogramSettingWidget;
}

class NephogramSettingWidget : public BasePage
{
    Q_OBJECT
  public:
    explicit NephogramSettingWidget(VisualWidgetService* postProcessingWidget,
                                    QWidget* parent = nullptr);
    ~NephogramSettingWidget();

    void beforeLoad() override;
    void beforeUnload() override;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;
    void selectGridNode(vtkObject* caller, unsigned long eventId, void* callData);

  private slots:
    // 单图模式
    void on_pushButton_clicked();
    void on_stepComboBox_currentIndexChanged(int index);
    void on_scalarComboBox_currentIndexChanged(int index);
    void on_resultDirComboBox_currentIndexChanged(int index);

    // 对比模式 UI 槽
    void on_compareEnableCheckBox_toggled(bool checked);
    void on_leftStepComboBox_currentIndexChanged(int index);
    void on_rightStepComboBox_currentIndexChanged(int index);
    void on_splitSlider_valueChanged(int value);

  private:
    // 单图辅助
    void ensureActorAttached();
    void syncScalarBarFromCurrent();
    void updateNephogram(const QString& selectedScalarName);
    void loadFileName(const QString& dataPath);
    void initResultDirectoryOptions();
    QString selectedResultDirectoryPath() const;
    void readTecplot(const QString& fileName);

    // 对比辅助
    void enterCompareMode();
    void exitCompareMode();
    void updateCompare();
    void updateViewports(double ratio); // 0~1
    void syncScalarBarForCompare(vtkDataArray* leftArr, vtkDataArray* rightArr);
    void fillCompareCombosFromSteps();
    vtkSmartPointer<vtkUnstructuredGrid> loadGridFromTecplot(const QString& fileName);
    vtkSmartPointer<vtkUnstructuredGrid> loadGridFromFile(const QString& fileName);
    QString resultDirectory() const;
    vtkDataArray* findScalarArray(vtkUnstructuredGrid* grid, const QString& scalarName,
                                  bool& usePointData) const;
    void updateLookupTableRange(vtkLookupTable* lut, double minV, double maxV) const;
    void configureMapper(vtkDataSetMapper* mapper, const QString& scalarName, bool usePointData,
                         double minV, double maxV, vtkLookupTable* lut) const;

  private:
    Ui::NephogramSettingWidget* ui{nullptr};
    VisualWidgetService* m_visualService{nullptr};

    // —— 单图对象 ——
    vtkSmartPointer<vtkActor> m_actor;
    vtkSmartPointer<vtkDataSetMapper> m_mapper;
    vtkSmartPointer<vtkUnstructuredGrid> m_grid;

    // —— 对比对象 ——
    bool m_compareEnabled{false};
    double m_splitRatio{0.5}; // 0~1

    vtkSmartPointer<vtkRenderer> m_leftRen;
    vtkSmartPointer<vtkRenderer> m_rightRen;

    // 保存基础渲染器（持有 ScalarBar），用于分层覆盖
    vtkSmartPointer<vtkRenderer> m_baseRenderer;

    vtkSmartPointer<vtkActor> m_leftActor;
    vtkSmartPointer<vtkActor> m_rightActor;

    vtkSmartPointer<vtkDataSetMapper> m_leftMapper;
    vtkSmartPointer<vtkDataSetMapper> m_rightMapper;

    vtkSmartPointer<vtkUnstructuredGrid> m_leftGrid;
    vtkSmartPointer<vtkUnstructuredGrid> m_rightGrid;

    // —— 状态/路径 ——
    QString m_dataPath;
    QString m_loadedFileName;
    QString m_currentArrayName; // 仅用于渲染同步（不做持久化）
    QMap<QString, QString> m_stepFileMap;
    bool m_initialized{false};   // 首次进入才读数据/填 step
    bool m_actorAttached{false}; // 避免重复 addActor

    void readVtu(const QString& fileName);
    void updateContourOverlay(const QString& scalarName, bool usePointData, double minV, double maxV);
    void clearContourOverlay();

    vtkSmartPointer<vtkActor> m_contourActor;
    std::vector<vtkSmartPointer<vtkBillboardTextActor3D>> m_contourLabelActors;
};

#endif
